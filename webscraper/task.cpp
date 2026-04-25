#include "webscraper/task.hpp"

#include <thread>

#include <tb/tb.h>

// Result

auto Result::GetType() const -> Type { return type; }

// ExternalTaskHandle

void ExternalTaskHandle::PushResult(Result result) const
{
    handle.delegator->PushExtraTasks(handle, false);
    handle.delegator->ProcessResult(handle, result);
}

// TaskGroup

void TaskGroup::Reset(bool is_available)
{
    results.clear();
    result_cb.reset();
    args_region.reset();
    extra_tasks_region.reset();
    for (std::atomic_flag& flag : extra_tasks_consumed_flags)
        flag.clear(std::memory_order_relaxed);
    results_region.reset();
    expecting.store(0, std::memory_order_relaxed);
    extra_tasks_available.store(0, std::memory_order_relaxed);
    available.store(is_available, std::memory_order_release);
}

// Worker

Worker::Worker(Delegator* d) : delegator(d)
{
    thread = std::thread([this] () {
        while (delegator->stay_alive.load(std::memory_order_relaxed)) {
            available.wait(true, std::memory_order_acquire);

            if (!delegator->stay_alive.load(std::memory_order_relaxed)) return;

            Task* next_task = nullptr;
            do {
                if (next_task != nullptr)
                    current_task = *next_task;

                Result result = current_task.callback(current_task.handle);
                next_task = delegator->PushExtraTasks(current_task.handle);

                delegator->ProcessResult(
                    current_task.handle,
                    result
                );
            } while (next_task != nullptr
                || delegator->PopNextTask(current_task).is_ok());

            available.store(true, std::memory_order_release);
            delegator->Wake();
        }
    });
}

Worker::~Worker()
{
    available.store(false, std::memory_order_relaxed);
    available.notify_one();
    thread.join();
}

// GroupHandle

void GroupHandle::SetResultCallback(UnboundResultCallback&& result_cb) const
{
    group->result_cb = result_cb.callback;
}

auto GroupHandle::QueueTasks(std::span<Task> tasks,
    std::initializer_list<ExternalTaskHandle> externals, bool is_reattempt) const
-> tb::error<QueueFullError>
{
    tb::scoped_guard wake = [this] { delegator->Wake(); };

    size_t old_expecting;
    if (is_reattempt) {
        old_expecting = group->expecting.load(std::memory_order_relaxed);
    } else {
        old_expecting = group->expecting.fetch_add(
            tasks.size() + externals.size(),
            std::memory_order_relaxed
        );

        if (old_expecting + tasks.size() + externals.size() > TaskGroup::MAX_TASKS)
            throw std::runtime_error { "Task limit exceeded" };
    }

    bool push_extra_tasks = !is_reattempt && old_expecting != 0 && tasks.size() > 0;

    for (Task& task : tasks) {
        task.handle = *this;
        if (push_extra_tasks)
            group->extra_tasks.push_back(task);
    }

    if (push_extra_tasks) {
        group->extra_tasks_available.fetch_add(tasks.size(), std::memory_order_release);
        return tb::ok;
    }

    if (delegator->task_queue.try_push_many(tasks).is_error())
        return QueueFullError {};

    return tb::ok;
}

auto GroupHandle::CreateExternalTask() const -> ExternalTaskHandle
{
    return ExternalTaskHandle { *this };
}

// Delegator

Delegator::Delegator(unsigned max_concurrent_tasks, unsigned max_task_groups)
: task_groups(max_task_groups)
{
    workers.emplace_all(max_concurrent_tasks, this);
    thread = std::thread([this] {
        while (stay_alive) {
            if (wake_up.load(std::memory_order_relaxed) == 0)
                wake_up.wait(0, std::memory_order_acquire);

            if (!stay_alive) return;

            RunNextTasks();
            wake_up.fetch_sub(1, std::memory_order_relaxed);
        }
    });
}

auto Delegator::NewTaskGroup() -> tb::result<GroupHandle, NoGroupsAvailableError>
{
    Wake();

    auto group = std::ranges::find_if(task_groups, [] (TaskGroup& g) {
        return g.available.exchange(false, std::memory_order_relaxed);
    });

    if (group == task_groups.end())
        return NoGroupsAvailableError {};

    uint32_t new_id = next_group_id.fetch_add(1, std::memory_order_relaxed);
    if (new_id == TaskGroup::INVALID_GROUP_ID)
        new_id = next_group_id.fetch_add(1, std::memory_order_relaxed);

    group->group_id.store(new_id, std::memory_order_relaxed);

    return GroupHandle { this, &(*group) };
}

void Delegator::Wake()
{
    if (wake_up.fetch_add(1, std::memory_order_relaxed) == 0)
        wake_up.notify_one();
}

void Delegator::RunNextTasks()
{
    std::span<Worker> workers_view = workers.view();
    while (true) {
        auto free_worker = std::ranges::find_if(workers_view, [] (Worker& w) {
            return w.available.load(std::memory_order_acquire);
        });

        if (free_worker == workers_view.end())
            return;

        if (PopNextTask(free_worker->current_task).is_error())
            return;

        free_worker->available.store(false, std::memory_order_release);
        free_worker->available.notify_one();
    }
}

auto Delegator::PushExtraTasks(GroupHandle handle, bool return_task) -> Task*
{
    TaskGroup* group = handle.group;

    Task* next_task = nullptr;
    size_t extra_task_size = group->extra_tasks_available.load(std::memory_order_acquire);
    for (size_t i = 0; i < extra_task_size; ++i) {
        std::atomic_flag& is_consumed = group->extra_tasks_consumed_flags[i];

        if (is_consumed.test_and_set(std::memory_order_acquire) == true)
            continue;

        Task& selected_task = group->extra_tasks.view()[i];
        if (return_task && next_task == nullptr) {
            next_task = &selected_task;
            continue;
        }

        if (task_queue.try_push(selected_task).is_error())
            is_consumed.clear();
    }

    return next_task;
}

void Delegator::ProcessResult(GroupHandle handle, Result result)
{
    TaskGroup* group = handle.group;
    uint32_t current_id = group->group_id.load(std::memory_order_acquire);

    size_t old_result_size = group->results.push_back(result);
    size_t old_expecting = group->expecting.load(std::memory_order_acquire);

    if (old_result_size + 1 < old_expecting)
        return;

    if (group->group_id.compare_exchange_strong(
        current_id, TaskGroup::INVALID_GROUP_ID,
        std::memory_order_release, std::memory_order_acquire
    ) == false)
        return;

    group->result_cb(handle, group->results.view());
    group->Reset(true);
}

auto Delegator::PopNextTask(Task& task) -> tb::error<tb::queue_empty_error>
{
    return task_queue.try_pop(task);
}

Delegator::~Delegator()
{
    stay_alive.store(false, std::memory_order_relaxed);
    Wake();
    thread.join();
}
