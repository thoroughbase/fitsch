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

#include <buxtehude/validate.hpp>

#include "common/product.hpp"
#include "common/util.hpp"

// Validation tests

const buxtehude::ValidationSeries VALIDATE_QUERY = {
    { "/terms"_json_pointer, [] (const json& j) { return j.is_array(); } }
};

// ResultCallbacks

void PrintProduct(const std::vector<Result>& results, App* app, const string& url)
{
    if (results.empty()) {
        Log(WARNING, "No product found at URL {}", url);
        return;
    }

    auto& product = results[0].Get<Product>();

    fmt::print("Product at URL `{}`:\n  {}: {} [{}]\n", url, product.name,
               product.item_price.ToString(), product.price_per_unit.ToString());

    app->database.PutProducts({product});
}

void SendQuery(const std::vector<Result>& results, App* app, const string& dest,
    const string& query_string, const StoreSelection& stores)
{
    ProductList list(10);
    bool upload = false;

    for (auto& result : results) {
        if (result.Type() != GENERIC_VALID) continue;
        auto& [queried_website, product_list]
            = result.Get<std::pair<bool, ProductList>>();

        list.Add(product_list);
        if (queried_website) upload = true;
    }

    std::vector<Product> products = list.AsProductVector();

    app->bclient->Write({ .type = "query-result", .dest = dest,
        .content = {
            { "items", products },
            { "term", query_string }
        }
    });

    if (upload) {
        Log(DEBUG, "Uploading query {}", query_string);
        auto qt = list.AsQueryTemplate(query_string, stores);
        app->database.PutQueryTemplates({ qt });
        if (list.size()) app->database.PutProducts(products);
    }
}

// TaskCallbacks
// Single product
Result TC_GetProduct_Parse(TaskContext ctx, const Store* store, const string& data)
{
    HTML html(data);

    return { GENERIC_VALID, new Product(store->GetProductAtURL(html)) };
}

Result TC_GetProduct_Fetch(TaskContext ctx, App* app, const string& url,
    const Store* store)
{
    auto handle = ctx.delegator->QueueExtraExternalTask(ctx.group_id);
    app->curl_driver->PerformTransfer(url, [handle, ctx, store] (auto data, auto url,
        CURLcode code) {
        ctx.delegator->QueueExtraTasks(ctx.group_id,
            Task { TC_GetProduct_Parse, store, string(data) }
        );
        handle.Finish({});
    });

    return {};
}

// Product list
Result TC_DoQuery_Parse(TaskContext ctx, const Store* store, const string& data,
    int depth)
{
    ProductList list = store->ParseProductSearch(data, depth);

    return { GENERIC_VALID, new std::pair<bool, ProductList>(true, std::move(list)) };
}

Result TC_DoQuery(TaskContext ctx, App* app, const string& query_string,
    const StoreSelection& stores, int depth)
{
    // Figure out what pages need to be fetched first
    for (StoreID id : stores) {
        const Store* store = app->GetStore(id);
        string url = store->GetProductSearchURL(query_string);

        auto handle = ctx.delegator->QueueExtraExternalTask(ctx.group_id);

        app->curl_driver->PerformTransfer(url,
        [handle, ctx, store, depth] (auto data, auto url, CURLcode code) {
            ctx.delegator->QueueExtraTasks(ctx.group_id,
                Task { TC_DoQuery_Parse, store, string(data), depth }
            );
            handle.Finish({});
        });
    }

    return {};
}

Result TC_GetQueriesDB(TaskContext ctx, App* app, const string& query_string,
    const StoreSelection& stores, int depth)
{

    // Get query template stored in database
    std::vector<string> queries = { std::string(query_string) };
    ProductList list(depth);
    auto qt = app->database.GetQueryTemplates(queries);

    StoreSelection missing;
    // Check if query is "complete" i.e. has the information we're looking for
    if (qt.empty()) {
        // If no query found, perform queries on all stores
        missing = stores;
    } else {
        // If query found, check to see if all stores contained & deep enough
        const QueryTemplate& q = qt[0];
        std::time_t now = std::time(nullptr);
        if (q.depth < depth || (!depth && q.depth)
            || (!q.depth && depth) || now - q.timestamp > ENTRY_EXPIRY_TIME) {
            // Query not deep enough or expired, redo all stores
            missing = stores;
        } else {
            // Are all the stores that we asked for there?
            if (!q.stores.Has(stores)) {
                missing = stores;
                missing.Remove(q.stores); // Just redo the missing ones
            }

            // Retrieve products from database as well
            std::vector<string> ids;
            ids.reserve(q.results.size());
            for (const auto& [id, info] : q.results)
                if (!(depth > 0 && info.relevance >= depth)) // Relevant?
                    ids.push_back(id);

            auto products = app->database.GetProducts(ids);
            if (ids.size()) {
                // TODO: Handle certain products not being found
            }

            for (const auto& p : products)
                list.emplace_back(p, q.results.at(p.id));
        }
    }

    if (missing.size()) {
        // Queue tasks to retrieve missing info
        ctx.delegator->QueueExtraTasks(ctx.group_id,
            Task { TC_DoQuery, app, query_string, missing, depth }
        );
    }

    return { GENERIC_VALID, new std::pair<bool, ProductList>(false, std::move(list)) };
}

// App

App::App(const string& cfg_path)
{
    CURLDriver::GlobalInit();
    buxtehude::Initialise([] (buxtehude::LogLevel l, const string& msg) {
        Log((LogLevel)l, "(buxtehude) {}", msg);
    });

    Log(INFO, "Starting Fitsch {}", VERSION);

    std::ifstream cfg_file(cfg_path);

    if (!cfg_file.is_open()) {
        Log(SEVERE, "Failed to open `config.json`. Shutting down");
        std::exit(1);
    }

    json config = json::parse(cfg_file);
    cfg_file.close();

    string mongouri = config["mongodb_uri"];

    curl_driver = std::make_unique<CURLDriver>(128);
    curl_driver->Run();

    database.Connect({mongouri});

    bclient = std::make_unique<buxtehude::Client>();
    bclient->preferences.format = buxtehude::MSGPACK;
    if (!bclient->IPConnect("localhost", 1637, "webscraper")) {
        Log(WARNING, "Failed to connect to buxtehude server");
        bclient->Close();
        bclient.reset();
        return;
    }

    Log(INFO, "Established connection to buxtehude server");

    bclient->AddHandler("query", [this] (buxtehude::Client& client,
                        const buxtehude::Message& msg) {
        if (!buxtehude::ValidateJSON(msg.content, VALIDATE_QUERY)) return;
        for (const json& j : msg.content["terms"]) {
            std::string term = j.get<string>();
            delegator.QueueTasks(
                { SendQuery, this, msg.src, term, stores::SuperValu.id },
                Task { TC_GetQueriesDB, this, term, stores::SuperValu.id, 10 }
            );
        }
    });

    bclient->Run();
}

App::~App()
{
    CURLDriver::GlobalCleanup();
}

void App::AddStore(const Store* store) { stores.emplace(store->id, store); }

const Store* App::GetStore(StoreID id) { return stores[id]; }

void App::GetProductAtURL(StoreID store, const string& item_url)
{
    if (!stores.contains(store)) {
        Log(WARNING, "Invalid store!");
        return;
    }

    delegator.QueueTasks({ PrintProduct, this, item_url },
        Task { TC_GetProduct_Fetch, this, item_url, stores[store] }
    );
}
