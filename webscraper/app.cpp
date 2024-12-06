#include "webscraper/app.hpp"

#include <iostream>
#include <fstream>
#include <thread>
#include <ctime>
#include <chrono>
#include <cstdlib>

#include <curl/curl.h>

#include <fmt/format.h>
#include <fmt/core.h>

#include <nlohmann/json.hpp>

#include "common/product.hpp"
#include "common/util.hpp"

// ResultCallbacks

void PrintProduct(App* a, const std::vector<TaskResult>& results)
{
    const TaskResult& tr = results[0];
    if (tr.value.empty()) {
        fmt::print("No product found at URL {}\n", tr.origin.args.str);
        return;
    }

    const Product& p = tr.value.First();
    fmt::print("Product at URL `{}`:\n  {}: {} [{}]\n", tr.origin.args.str, p.name,
               p.item_price.str(), p.price_per_unit.str());

    a->database.PutProducts({p});
}

void PrintQuery(App* a, const std::vector<TaskResult>& results)
{
    ProductList list;
    bool upload = false;
    for (const auto& r : results) {
        list.Add(r.value);
        if (r.flags & RF_QUERIED_WEBSITE) upload = true;
    }

    const TaskResult& tr = results[0];
    const string& querystr = tr.origin.args.str;
    list.depth = tr.origin.args.depth;

    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        // Sort by price if units don't match
        return (a.first.price_per_unit.first == b.first.price_per_unit.first) ?
                a.first.price_per_unit.second < b.first.price_per_unit.second :
                a.first.item_price < b.first.item_price;
    });

    string text = fmt::format("Results for query `{}`:\n================\n", querystr);

    for (const auto& pair : list) {
        const Product& p = pair.first;
        text += fmt::format("[{}] {}: {} [{}]\n", a->GetStore(p.store)->prefix,
                    p.name, p.item_price.str(), p.price_per_unit.str());
    }

    fmt::print("{}", text);

    if (upload) { // Not purely from database
        Log(DEBUG, "Uploading query {}", querystr);
        auto qt = list.AsQueryTemplate(querystr, tr.origin.args.stores);
        a->database.PutQueryTemplates({qt});
        if (list.size()) a->database.PutProducts(list.AsProductVector());
    }
}

// TaskCallbacks

void TC_GetProductAtURL(TaskRunner* runner, const Task& t)
{
    HTML page(runner->curl, t.args.str);
    const Store* store = runner->app->GetStore(t.args.stores[0]);

    Product r = store->GetProductAtURL(page);
    ProductList list(r);
    runner->Finish(TaskResult {t, list, RF_QUERIED_WEBSITE});
}

void TC_DoQuery(TaskRunner* runner, const Task& t)
{
    char* escq = curl_easy_escape(runner->curl, t.args.str.data(), t.args.str.size());
    const Store* store = runner->app->GetStore(t.args.stores[0]);

    ProductList ps = store->SearchProducts(escq, runner->curl, t.args.depth);
    runner->Finish(TaskResult {t, ps});

    curl_free(escq);
}

void TC_GetQueriesDB(TaskRunner* runner, const Task& t)
{
    TaskResult tr;

    // Get query template stored in database
    std::vector<string> querystr = {t.args.str};
    ProductList list(t.args.depth);
    auto qt = runner->app->database.GetQueryTemplates(querystr);

    StoreSelection missing;
    // Check if query is "complete" i.e. has the information we're looking for
    if (qt.empty()) {
        // If no query found, perform queries on all stores
        missing = t.args.stores;
    } else {
        // If query found, check to see if all stores contained & deep enough
        const QueryTemplate& q = qt[0];
        std::time_t now = std::time(nullptr);
        if (q.depth < t.args.depth || (!t.args.depth && q.depth)
            || (!q.depth && t.args.depth) || now - q.timestamp > ENTRY_EXPIRY_TIME) {
            // Query not deep enough or expired, redo all stores
            missing = t.args.stores;
        } else {
            // Are all the stores that we asked for there?
            if (!q.stores.Has(t.args.stores)) {
                missing = t.args.stores;
                missing.Remove(q.stores); // Just redo the missing ones
            }

            // Retrieve products from database as well
            std::vector<string> ids;
            ids.reserve(q.results.size());
            for (const auto& [id, info] : q.results)
                if (!(t.args.depth > 0 && info.relevance >= t.args.depth)) // Relevant?
                    ids.push_back(id);

            auto products = runner->app->database.GetProducts(ids);
            if (ids.size()) {
                // TODO: Handle certain products not being found
            }

            for (const auto& p : products)
                list.emplace_back(p, q.results.at(p.id));
        }
    }

    if (missing.size()) {
        std::vector<Task> tasks;
        tasks.reserve(missing.size());
        for (StoreID id : missing)
            tasks.push_back({
                TaskArgs { t.args.str, id, t.args.depth },
                TC_DoQuery
            });
        // Queue tasks to retrieve missing info
        runner->app->delegator.QueueExtraTasks(t.group_id, tasks);
        tr.flags |= RF_QUERIED_WEBSITE;
    }
    tr.origin = t;
    tr.value = list;
    // Queue result to be processed
    runner->Finish(tr);
}

// App

App::App(const string& cfg_path)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    Log(INFO, "Starting Fitsch {}", VERSION);

    if (!(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_THREADSAFE)) {
        Log(SEVERE, "CURL NOT THREADSAFE!");
        std::exit(1);
    }

    std::ifstream cfg_file(cfg_path);

    if (!cfg_file.is_open()) {
        Log(SEVERE, "Failed to open `config.json`. Shutting down");
        std::exit(1);
    }

    json config = json::parse(cfg_file);
    cfg_file.close();

    int max_conc = config["max_concurrent"];
    string mongouri = config["mongodb_uri"];

    Log(INFO, "Max concurrent tasks = {}", max_conc);

    delegator.AddRunners(this, max_conc);
    database.Connect({mongouri});
}

App::~App()
{
    curl_global_cleanup();
}

void App::AddStore(const Store* store) { stores.emplace(store->id, store); }

const Store* App::GetStore(StoreID id) { return stores[id]; }

void App::GetProductAtURL(StoreID store, const string& item_url)
{
    if (!stores.contains(store)) {
        Log(WARNING, "Invalid store!");
        return;
    }

    delegator.QueueTasks({ Task {
        TaskArgs { item_url, store, 0 },
        TC_GetProductAtURL
    }}, PrintProduct);
}

void App::DoQuery(const StoreSelection& sel, const string& query, int depth)
{
    delegator.QueueTasks({ Task {
        TaskArgs { query, sel, depth },
        TC_GetQueriesDB
    }}, PrintQuery);
}

// TaskRunner

TaskRunner::TaskRunner(App* a) : app(a)
{
    curl = curl_easy_init();
    if (!curl) {
        Log(SEVERE, "Error initialising curl for task runner");
        return;
    }

    ready = true;
}

TaskRunner::TaskRunner(TaskRunner&& other) : app(other.app)
{
    curl = other.curl;
    other.curl = nullptr;

    ready = static_cast<bool>(other.ready);
}

TaskRunner::~TaskRunner() { if (curl) curl_easy_cleanup(curl); }

void TaskRunner::RunTask(const Task& t)
{
    if (!ready) return;

    ready = false;

    std::thread thr(t.callback, this, t);
    thr.detach();
}

void TaskRunner::Finish(const TaskResult& r)
{
    ready = true;
    app->delegator.TaskDone(this, r);
}

bool TaskRunner::Ready() { return ready; }

// Delegator

Delegator::Delegator() {}

Delegator::Delegator(App* a, int task_runners)
{
    AddRunners(a, task_runners);
}

void Delegator::AddRunners(App* a, int task_runners)
{
    runners.reserve(task_runners);
    for (int i = 0; i < task_runners; ++i)
        runners.emplace_back(a);
}

unsigned Delegator::QueueTasks(const std::vector<Task>& tasks, ResultCallback c)
{
    static unsigned groupid = 0;

    std::lock_guard<std::mutex> qguard(queue_guard);
    ++groupid;

    for (Task t : tasks) {
        t.group_id = groupid;
        task_queue.push(t);
    }

    Delegate();
    results.emplace_back(groupid, (unsigned) tasks.size(), c, std::vector<TaskResult> {});

    return groupid;
}

void Delegator::QueueExtraTasks(unsigned gid, const std::vector<Task>& tasks)
{
    { // Results guard
    std::lock_guard<std::mutex> rguard(results_guard);
    auto it = std::find_if(results.begin(), results.end(), [gid](const auto& tuple){
        return tuple.groupid == gid;
    });

    if (it == results.end()) {
        Log(WARNING, "Tried to add extra tasks, group id {} not found", gid);
        return;
    }

    it->taskno += tasks.size();
    }

    std::lock_guard<std::mutex> qguard(queue_guard);

    for (Task t : tasks) {
        t.group_id = gid;
        task_queue.push(t);
    }

    Delegate();
}

void Delegator::TaskDone(TaskRunner* runner, const TaskResult& r)
{
    { // Results guard
    std::lock_guard<std::mutex> rguard(results_guard);
    auto it = std::find_if(results.begin(), results.end(), [r](const auto& tuple){
        return tuple.groupid == r.origin.group_id;
    });

    // Add results (& call ResultsCallback if task group finished)
    it->results.push_back(r);
    if (it->results.size() >= it->taskno) {
        // TODO: Keep tabs on threads
        std::thread thr(it->cb, runner->app, it->results);
        thr.detach();

        results.erase(it);
    }
    }

    // Run next task
    std::lock_guard<std::mutex> qguard(queue_guard);

    if (task_queue.size()) {
        runner->RunTask(task_queue.front());
        task_queue.pop();
    }
}

void Delegator::Delegate()
{
    TaskRunner* runner = nullptr;
    while ((runner = FirstAvailableRunner()) && task_queue.size()) {
        runner->RunTask(task_queue.front());
        task_queue.pop();
    }
}

TaskRunner* Delegator::FirstAvailableRunner()
{
    for (auto it = runners.begin(); it != runners.end(); ++it)
        if ((*it).Ready()) return &(*it);

    return nullptr;
}
