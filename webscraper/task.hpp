#pragma once

#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <span>
#include <thread>
#include <type_traits>
#include <memory>


struct TaskContext;
struct Result;
struct ResultContainer;
class Delegator;

using TaskCallback = std::function<Result(TaskContext)>;
using ResultCallback = std::function<void(const std::vector<Result>&)>;

using BoundDeleter = std::function<void()>;

enum class ResultType
{
    EMPTY, GENERIC_ERROR, GENERIC_VALID, GENERIC_SINGLE, GENERIC_VECTOR
};

struct Task
{
    template<typename Callable, typename... Args>
    Task(Callable&& cb, Args&& ...args)
    : task_cb(std::bind(std::forward<Callable>(cb), std::placeholders::_1,
              std::forward<Args>(args)...)) {}

    TaskCallback task_cb;
    unsigned group_id;
};

struct Result
{
public:
    Result() = default;

    template<typename T, typename Deleter = std::default_delete<T>>
    Result(ResultType rt, T* ptr, Deleter&& ubdel = std::default_delete<T>{})
    : deleter(std::bind(std::forward<Deleter>(ubdel), ptr)), data(ptr), type(rt) {}

    Result(ResultType rt, std::nullptr_t ptr) : type(rt) {}

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    Result(Result&& other) noexcept;
    Result& operator=(Result&& other) noexcept;

    template<typename T>
    const T& Get() const { return *static_cast<T*>(data); }
    ResultType Type() const;

    ~Result();

private:
    BoundDeleter deleter = nullptr;
    void* data = nullptr;
    ResultType type = ResultType::EMPTY;
};

struct ResultsContainer
{
    ResultCallback result_cb;
    std::vector<Result> results;
    size_t expecting = 0;
};

struct TaskContext
{
    Delegator& delegator;
    unsigned group_id;
};

struct UnboundResultCallback
{
    template<typename Callable, typename... Args>
    UnboundResultCallback(Callable&& cb, Args&& ...args)
    : result_cb(std::bind(std::forward<Callable>(cb), std::placeholders::_1,
                std::forward<Args>(args)...)) {}

    ResultCallback result_cb;
};

struct ExternalTaskHandle
{
public:
    ExternalTaskHandle(Delegator& delegator, unsigned id);

    void Finish(Result&& result) const;
private:
    Delegator& delegator;
    unsigned group_id;
};

class Delegator
{
public:
    Delegator(int max_tasks);

    template<size_t N=std::dynamic_extent>
    unsigned QueueTasks(UnboundResultCallback&& ub_rcb, std::span<Task, N> tasks)
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

    template<size_t N=std::dynamic_extent>
    void QueueExtraTasks(unsigned id, std::span<Task, N> tasks)
    {
        std::lock_guard<std::mutex> guard(task_mutex);

        auto& [key, container_ref] = *(results.find(id));
        container_ref.expecting += tasks.size();
        TryRun(id, tasks);
    }

    ExternalTaskHandle QueueExternalTask(UnboundResultCallback&& ub_rcb);
    ExternalTaskHandle QueueExtraExternalTask(unsigned id);

    void RunNextTask();

private:
    template<size_t N=std::dynamic_extent>
    void TryRun(unsigned id, std::span<Task, N> tasks)
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

    void ProcessResult(unsigned id, Result&& result);

private:
    friend ExternalTaskHandle;
    std::queue<Task> task_queue;
    std::unordered_map<unsigned, ResultsContainer> results;

    std::mutex task_mutex;
    std::mutex results_mutex;

    std::atomic<int> running_tasks = 0;
    int max_concurrent_tasks = 16;

    unsigned current_group_id = 0;
};
