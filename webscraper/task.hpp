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
    void Finish(Result&& result) const;
private:
    friend Delegator;
    ExternalTaskHandle(Delegator& delegator, unsigned id);

    Delegator& delegator;
    unsigned group_id;
};

class Delegator
{
public:
    Delegator(int max_tasks);

    unsigned QueueTasks(UnboundResultCallback&& ub_rcb, std::span<Task> tasks);
    void QueueExtraTasks(unsigned id, std::span<Task> tasks);
    ExternalTaskHandle QueueExternalTask(UnboundResultCallback&& ub_rcb);
    ExternalTaskHandle QueueExtraExternalTask(unsigned id);

    void RunNextTask();

private:
    void TryRun(unsigned id, std::span<Task> tasks);
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
