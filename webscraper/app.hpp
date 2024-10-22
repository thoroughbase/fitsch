#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>

#include <curl/curl.h>

#include "common/product.hpp"
#include "webscraper/stores.hpp"
#include "webscraper/database.hpp"

#define VERSION "0.0.1"
#define ENTRY_EXPIRY_TIME 86400 * 2

using std::string;

class App;
class TaskRunner;

struct Task;
struct TaskResult;

using ResultFlags = int;
inline const ResultFlags RF_QUERIED_WEBSITE = 0b01;

using ResultCallback = std::function<void(App*, const std::vector<TaskResult>&)>;
using TaskCallback = std::function<void(TaskRunner*, const Task&)>;

struct TaskArgs
{
    string str;
    StoreSelection stores;
    int depth;
};

struct Task
{
    TaskArgs args;
    TaskCallback callback;

    // Assigned by the Delegator
    unsigned group_id;
};

struct TaskResult
{
    Task origin;
    ProductList value;
    ResultFlags flags;
};

class TaskRunner
{
public:
    TaskRunner(App* a);
    TaskRunner(TaskRunner&& runner);
    ~TaskRunner();

    void RunTask(const Task& t);
    void Finish(const TaskResult& r);
    bool Ready();

    CURL* curl;
    App* app;
private:
    std::atomic<bool> ready = false;
};

struct ResultTuple
{
    ResultTuple(unsigned g, unsigned t, const ResultCallback& c,
                const std::vector<TaskResult>& r)
        : groupid(g), taskno(t), cb(c), results(r) {}
    unsigned groupid, taskno;
    ResultCallback cb;
    std::vector<TaskResult> results;
};

class Delegator
{
public:
    Delegator();
    Delegator(App* a, int task_runners=8);

    void AddRunners(App* a, int task_runners);

    // If `tasks` contains more than one task, all tasks will be queued together as a
    // subtasks. Returns the group id.
    unsigned QueueTasks(const std::vector<Task>& tasks, const ResultCallback& c);
    void QueueExtraTasks(unsigned gid, const std::vector<Task>& tasks);

    // Signals that a runner is done with a task
    void TaskDone(TaskRunner* runner, const TaskResult& r);

private:
    // Returns nullptr if there are no available TaskRunners
    TaskRunner* FirstAvailableRunner();
    void Delegate();

    std::vector<TaskRunner> runners;
    std::queue<Task> task_queue;
    std::vector<ResultTuple> results;

    std::mutex queue_guard, results_guard;
};

class App
{
public:
    App(string cfg_path);
    ~App();

    void AddStore(const Store* store);
    const Store* GetStore(StoreID id);

    void GetProductAtURL(StoreID store, const string& item_url);
    void DoQuery(const StoreSelection& stores, const string& query, int depth=0);

    Delegator delegator;
    Database database;

private:
    std::map<StoreID, const Store*> stores;
};
