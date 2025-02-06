#include "webscraper/task.hpp"

#include <thread>
#include <utility>

// Result

Result::Result(Result&& other) noexcept : type(other.type),
    deleter(std::move(other.deleter))
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

ExternalTaskHandle::ExternalTaskHandle(Delegator* d, unsigned gid)
    : delegator(d), group_id(gid) {}

void ExternalTaskHandle::Finish(Result&& result) const
{
    delegator->ProcessResult(group_id, std::move(result));
}

// Delegator

Delegator::Delegator(int max_tasks) : max_concurrent_tasks(max_tasks) {}

void Delegator::RunNextTask()
{
    std::lock_guard<std::mutex> tasks_guard(task_mutex);
    std::lock_guard<std::mutex> results_guard(results_mutex);

    if (task_queue.size()) {
        Task& task = task_queue.front();
        TryRun(task.group_id, std::move(task));
        task_queue.pop();
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

    return { this, current_group_id };
}

ExternalTaskHandle Delegator::QueueExtraExternalTask(unsigned id)
{
    std::lock_guard<std::mutex> results_guard(results_mutex);

    auto& [key, container_ref] = *(results.find(id));
    ++container_ref.expecting;

    return { this, id };
}

void Delegator::TryRun(unsigned id, Task&& task)
{
    task.group_id = id;
    if (running_tasks >= max_concurrent_tasks) {
        task_queue.emplace(std::move(task));
        return;
    }

    ++running_tasks;

    std::thread thread([this] (Task&& task) {
        Result result = task.task_cb(TaskContext { task.group_id, this });
        ProcessResult(task.group_id, std::move(result));

        --running_tasks;
        RunNextTask();
    }, std::move(task));

    thread.detach();
}

void Delegator::ProcessResult(unsigned group_id, Result&& result)
{
    std::lock_guard<std::mutex> results_guard(results_mutex);

    auto& [id, container] = *(results.find(group_id));
    auto& result_vec = container.results;

    result_vec.emplace_back(std::move(result));

    if (result_vec.size() >= container.expecting) {
        std::erase_if(result_vec, [] (Result& result) {
            return result.Type() == EMPTY;
        });
        container.result_cb(result_vec);
        results.erase(id);
    }
}
