#pragma once

#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <type_traits>
#include <memory>

struct TaskContext;
struct Result;
struct ResultContainer;
class Delegator;

using TaskCallback = std::function<Result(TaskContext)>;
using ResultCallback = std::function<void(const std::vector<Result>&)>;

using BoundDeleter = std::function<void()>;

enum ResultType
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
    : type(rt), data(ptr), deleter(std::bind(std::forward<Deleter>(ubdel), ptr)) {}

    Result(ResultType rt, std::nullptr_t ptr) : type(rt) {}

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    Result(Result&& other) noexcept;
    Result& operator=(Result&& other) noexcept;

    template<typename T>
    const T& Get() const { return *(T*)data; }
    ResultType Type() const;

    ~Result();

private:
    ResultType type = EMPTY;
    void* data = nullptr;
    BoundDeleter deleter = nullptr;
};

struct ResultsContainer
{
    int expecting = 0;
    std::vector<Result> results;
    ResultCallback result_cb;
};

struct TaskContext
{
    unsigned group_id;
    Delegator* delegator;
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
    ExternalTaskHandle(Delegator* delegator, unsigned id);

    void Finish(Result&& result) const;
private:
    Delegator* delegator;
    unsigned group_id;
};

class Delegator
{
public:
    Delegator(int max_tasks);

    template<std::same_as<Task>... Tasks>
    unsigned QueueTasks(UnboundResultCallback&& ub_rcb, Tasks&& ...tasks)
    {
        std::lock_guard<std::mutex> tasks_guard(task_mutex);
        std::lock_guard<std::mutex> results_guard(results_mutex);

        ++current_group_id;

        auto [iterator, success] = results.emplace(current_group_id, ResultsContainer {
            .expecting = sizeof...(tasks),
            .results = {},
            .result_cb = std::move(ub_rcb.result_cb)
        });

        auto& [key, container_ref] = *iterator;

        container_ref.results.reserve(sizeof...(tasks));

        TryRun(current_group_id, std::forward<Tasks>(tasks)...);
        return current_group_id;
    }

    template<std::same_as<Task>... Tasks>
    void QueueExtraTasks(unsigned id, Tasks&& ...tasks)
    {
        std::lock_guard<std::mutex> guard(task_mutex);

        auto& [key, container_ref] = *(results.find(id));
        container_ref.expecting += sizeof...(tasks);
        TryRun(id, std::forward<Tasks>(tasks)...);
    }

    ExternalTaskHandle QueueExternalTask(UnboundResultCallback&& ub_rcb);
    ExternalTaskHandle QueueExtraExternalTask(unsigned id);

    void RunNextTask();

private:
    template<std::same_as<Task>... Tasks>
    void TryRun(unsigned id, Task&& task, Tasks&& ...tasks)
    {
        TryRun(id, std::forward<Task>(task));
        TryRun(id, std::forward<Tasks>(tasks)...);
    }

    void TryRun(unsigned id, Task&& task);

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
