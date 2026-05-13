#pragma once

#include <vector>
#include <atomic>
#include <span>
#include <thread>
#include <type_traits>
#include <memory>

#include <tb/tb.h>

// Fresh task groups are made by calling Delegator::NewTaskGroup() - if successful
// (i.e. a task group is available), this returns a GroupHandle.
//
// A hard limit TaskGroup::MAX_TASKS is imposed on the number of tasks that can be
// created within one TaskGroup - exceeding this limit is illegal and throws a
// runtime_error.
//
// A result callback should always be set before queuing any tasks.
// External tasks must not complete before at least one attempt to call QueueTasks,
// with their handles passed in to the function. ExternalTaskHandle::PushResult must
// be called before the handle goes out of scope.
//
// When queuing tasks for the first time, the Delegator will attempt to push all of them
// directly into the task queue. QueueTask() will return a QueueFullError if this fails.
//
// When reattempting to queue tasks, the is_reattempt parameter should be set to true.
// Ad-hoc tasks are stored in the TaskGroup and an attempt to push them into the queue
// will be made when a Worker completes a task in the TaskGroup. Ad-hoc QueueTask calls
// (e.g. inside a Task callback) can never fail - the returned tb::error can safely be
// ignored.
//
// Arguments and results can be allocated using AllocateArg and AllocateResult
// respectively.

struct ExternalTaskHandle;
struct GroupHandle;
struct Result;
struct Task;
struct TaskGroup;
struct Worker;

class Delegator;

using ResultCallback = tb::func<void, GroupHandle, std::span<Result>>;
using TaskCallback = tb::func<Result, GroupHandle>;
using Arena = tb::thread_safe_memory_arena;

struct Result
{
    enum Type
    {
        EMPTY, GENERIC_ERROR, GENERIC_VALID, GENERIC_SINGLE, GENERIC_VECTOR
    };

    void* data = nullptr;
    Type type = EMPTY;

    auto GetType() const -> Type;

    template<typename T>
    auto Get() const -> T& { return *static_cast<T*>(data); }

    constexpr static auto Error() -> Result
    {
        return { nullptr, GENERIC_ERROR };
    }
};

struct QueueFullError {};

struct GroupHandle
{
    TaskGroup* group = nullptr;
    Delegator* delegator = nullptr;

    template<typename Callable, typename... Args>
        requires std::is_invocable_r_v<void, Callable, GroupHandle,
            std::span<Result>, Args...>
    void SetResultCallback(Callable&& cb, Args&&... args) const;

    auto QueueTasks(std::span<Task> tasks,
        std::initializer_list<ExternalTaskHandle> externals = {},
        bool is_reattempt = false) const
    -> tb::error<QueueFullError>;
    auto CreateExternalTask() const -> ExternalTaskHandle;

    template<typename T, typename... Args>
        requires tb::allocator_constructible<T, tb::allocator_type<T>, Args...>
    auto Allocate(Arena& region, Args&&... args) const -> T*;

    template<typename T, typename... Args>
    auto Allocate(Arena& region, Args&&... args) const -> T*;

    template<typename T, typename... Args>
    auto AllocateArg(Args&&... args) const -> T*;

    template<typename T, typename... Args>
    auto AllocateResult(Args&&... args) const -> T*;

    GroupHandle(Delegator* delegator, TaskGroup* group)
    : group(group), delegator(delegator) {}
    GroupHandle() = default;
};

struct Task
{
    Task() = default;

    template<typename Callable, typename... Args>
        requires std::is_invocable_r_v<Result, Callable, GroupHandle, Args...>
    Task(Callable&& cb, Args&& ...args)
    : callback ([args..., cb] (GroupHandle ctx) -> Result {
        return cb(ctx, args...);
    }) {}

    TaskCallback callback;
    GroupHandle handle;
};

struct TaskGroup
{
    constexpr static size_t MEMORY_POOL_SIZE = 1024 * 1024;
    constexpr static size_t MAX_TASKS = 32;
    constexpr static size_t ARGS_MEMORY = 512;
    constexpr static size_t EXTRA_TASK_MEMORY = MAX_TASKS * sizeof(Task);
    constexpr static size_t RESULT_VEC_MEMORY = MAX_TASKS * sizeof(Result);
    constexpr static uint32_t INVALID_GROUP_ID = std::numeric_limits<uint32_t>::max();

    tb::dynamically_allocated_array<std::byte, MEMORY_POOL_SIZE> memory {};
    ResultCallback result_cb;
    Arena result_vec_region  = std::span { memory.begin(), RESULT_VEC_MEMORY },
          args_region        = std::span { result_vec_region.end(), ARGS_MEMORY },
          extra_tasks_region = std::span { args_region.end(), EXTRA_TASK_MEMORY },
          results_region     = std::span { extra_tasks_region.end(), memory.end() };

    std::array<std::atomic_flag, MAX_TASKS> extra_tasks_consumed_flags {};
    tb::fixed_size_vector<Result> results { result_vec_region };
    tb::fixed_size_vector<Task> extra_tasks { extra_tasks_region };
    std::atomic<size_t> extra_tasks_available { 0 };
    std::atomic<size_t> expecting { 0 };
    std::atomic<uint32_t> group_id { 0 };
    std::atomic<bool> available { true };

    void Reset(bool is_available);
};

struct ExternalTaskHandle
{
    GroupHandle handle;

    void PushResult(Result result) const;
};

template<typename Callable, typename... Args>
    requires std::is_invocable_r_v<void, Callable, GroupHandle,
        std::span<Result>, Args...>
void GroupHandle::SetResultCallback(Callable&& cb, Args&&... args) const
{
    group->result_cb = [args..., cb] (GroupHandle handle, std::span<Result> results) {
        cb(handle, results, args...);
    };
}

template<typename T, typename... Args>
    requires tb::allocator_constructible<T, tb::allocator_type<T>, Args...>
auto GroupHandle::Allocate(Arena& region, Args&&... args) const -> T*
{
    return region.allocate_object<T>(
        std::forward<Args>(args)...,
        tb::allocator_type<T> { region }
    );
}

template<typename T, typename... Args>
auto GroupHandle::Allocate(Arena& region, Args&&... args) const -> T*
{
    return region.allocate_object<T>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
auto GroupHandle::AllocateArg(Args&&... args) const -> T*
{
    return Allocate<T>(group->args_region, std::forward<Args>(args)...);
}

template<typename T, typename... Args>
auto GroupHandle::AllocateResult(Args&&... args) const -> T*
{
    return Allocate<T>(group->results_region, std::forward<Args>(args)...);
}

struct Worker
{
    Worker() = default;
    Worker(Delegator* delegator);

    ~Worker();

    Task current_task;
    std::thread thread;
    Delegator* delegator = nullptr;
    std::atomic<bool> available { true };
};

struct NoGroupsAvailableError {};

class Delegator
{
public:
    Delegator(unsigned max_concurrent_tasks = 4, unsigned max_task_groups = 32);

    auto NewTaskGroup() -> tb::result<GroupHandle, NoGroupsAvailableError>;

    ~Delegator();

private:
    friend Worker;
    friend GroupHandle;
    friend ExternalTaskHandle;
    void Wake();
    void RunNextTasks();
    auto PushExtraTasks(GroupHandle group, bool return_task = true) -> Task*;
    void ProcessResult(GroupHandle group, Result result);
    auto PopNextTask(Task& task) -> tb::error<tb::queue_empty_error>;

    tb::mpmc_queue<Task, 128> task_queue;
    std::vector<TaskGroup> task_groups;
    tb::dynamically_allocated_array<Worker, std::dynamic_extent> workers;
    std::thread thread;
    std::atomic<size_t> wake_up { 0 };
    std::atomic<uint32_t> next_group_id { 0 };
    std::atomic<bool> stay_alive { true };
};
