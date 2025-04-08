#include "webscraper/task.hpp"

#include <thread>
#include <utility>

#include "common/util.hpp"

// Result

Result::Result(Result&& other) noexcept : deleter(std::move(other.deleter)),
    type(other.type)
{
    data = std::exchange(other.data, nullptr);
}

Result& Result::operator=(Result&& other) noexcept
{
    type = other.type;
    data = std::exchange(other.data, nullptr);
    deleter = std::move(other.deleter);

    return *this;
}

ResultType Result::Type() const { return type; }

Result::~Result() { if (deleter && data) deleter(); }

// ExternalTaskHandle

ExternalTaskHandle::ExternalTaskHandle(Delegator& d, unsigned gid)
    : delegator(d), group_id(gid) {}

void ExternalTaskHandle::Finish(Result&& result) const
{
    delegator.ProcessResult(group_id, std::move(result));
}

// Delegator

Delegator::Delegator(int max_tasks) : max_concurrent_tasks(max_tasks) {}

unsigned Delegator::QueueTasks(UnboundResultCallback&& ub_rcb, std::span<Task> tasks)
{
    std::lock_guard<std::mutex> tasks_guard(task_mutex);
    std::lock_guard<std::mutex> results_guard(results_mutex);

    ++current_group_id;

    auto [iterator, success] = results.emplace(current_group_id, ResultsContainer {
        .expecting = tasks.size(),
        .results = {},
        .result_cb = std::move(ub_rcb.result_cb)
    });

    auto& [key, container_ref] = *iterator;

    container_ref.results.reserve(tasks.size());

    TryRun(current_group_id, tasks);
    return current_group_id;
}

void Delegator::QueueExtraTasks(unsigned id, std::span<Task> tasks)
{
    std::lock_guard<std::mutex> guard(task_mutex);

    auto& [key, container_ref] = *(results.find(id));
    container_ref.expecting += tasks.size();
    TryRun(id, tasks);
}

void Delegator::RunNextTask()
{
    std::lock_guard<std::mutex> tasks_guard(task_mutex);
    std::lock_guard<std::mutex> results_guard(results_mutex);

    if (task_queue.size()) {
        Task& task = task_queue.front();
        TryRun(task.group_id, tb::make_span({ std::move(task) }));
        task_queue.pop();
    }
}

void Delegator::TryRun(unsigned id, std::span<Task> tasks)
{
    for (Task& task : tasks) {
        task.group_id = id;
        if (running_tasks >= max_concurrent_tasks) {
            task_queue.emplace(std::move(task));
            return;
        }

        ++running_tasks;

        std::thread thread([this] (Task&& task) {
            Result result = task.task_cb(TaskContext {
                .delegator = *this,
                .group_id = task.group_id
            });
            ProcessResult(task.group_id, std::move(result));

            --running_tasks;
            RunNextTask();
        }, std::move(task));

        thread.detach();
    }
}

ExternalTaskHandle Delegator::QueueExternalTask(UnboundResultCallback&& ub_rcb)
{
    std::lock_guard<std::mutex> results_guard(results_mutex);

    ++current_group_id;

    results.emplace(current_group_id, ResultsContainer {
        .expecting = 1,
        .results = {},
        .result_cb = std::move(ub_rcb.result_cb)
    });

    return { *this, current_group_id };
}

ExternalTaskHandle Delegator::QueueExtraExternalTask(unsigned id)
{
    std::lock_guard<std::mutex> results_guard(results_mutex);

    auto& [key, container_ref] = *(results.find(id));
    ++container_ref.expecting;

    return { *this, id };
}

void Delegator::ProcessResult(unsigned group_id, Result&& result)
{
    std::lock_guard<std::mutex> results_guard(results_mutex);

    auto& [id, container] = *(results.find(group_id));
    std::vector<Result>& result_vec = container.results;

    result_vec.emplace_back(std::move(result));

    if (result_vec.size() >= container.expecting) {
        std::erase_if(result_vec, [] (Result& result) {
            return result.Type() == ResultType::EMPTY;
        });
        container.result_cb(result_vec);
        results.erase(id);
    }
}
