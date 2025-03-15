#include "webscraper/app.hpp"

#include <fstream>
#include <cstdlib>
#include <ranges>

#include <fmt/format.h>
#include <fmt/core.h>

#include <nlohmann/json.hpp>

#include <buxtehude/validate.hpp>

#include "common/product.hpp"
#include "common/util.hpp"
#include "common/validate.hpp"

// ResultCallbacks

static void PrintProduct(const std::vector<Result>& results, App* app,
    const std::string& url)
{
    if (results.empty()) {
        Log(LogLevel::WARNING, "No product found at URL {}", url);
        return;
    }

    if (results[0].Type() == ResultType::GENERIC_ERROR) {
        Log(LogLevel::WARNING, "Error whilst fetching/parsing product at URL {}", url);
        return;
    }

    auto& product = results[0].Get<Product>();

    fmt::print("Product at URL `{}`:\n  {}: {} [{}]\n", url, product.name,
               product.item_price.ToString(), product.price_per_unit.ToString());

    app->database.PutProducts(tb::make_span({ product }));
}

static void SendQuery(const std::vector<Result>& results, App* app,
    const std::string& dest, const std::string& query_string,
    const StoreSelection& stores, int request_id)
{
    ProductList list;
    bool upload = false;

    for (auto& result : results) {
        if (result.Type() != ResultType::GENERIC_VALID) continue;
        auto& [queried_website, product_list]
            = result.Get<std::pair<bool, ProductList>>();

        list.Add(product_list);
        if (queried_website) upload = true;
    }

    std::vector<Product> products = list.AsProductVector();

    app->bclient->Write({ .type = "query-result", .dest = dest,
        .content = {
            { "items", products },
            { "term", query_string },
            { "request-id", request_id }
        }
    }).if_err([app] (buxtehude::WriteError e) {
        app->bclient->Close();
        Log(LogLevel::WARNING, "Failed to write back query-result - connection closed");
    });

    if (upload) {
        Log(LogLevel::DEBUG, "Uploading query {}", query_string);
        auto qt = list.AsQueryTemplate(query_string, stores);
        app->database.PutQueryTemplates(tb::make_span({ qt }));
        if (!list.products.empty()) app->database.PutProducts(products);
    }
}

// TaskCallbacks
// Single product
static Result TC_GetProduct_Parse(TaskContext ctx, const Store* store,
    const std::string& data)
{
    HTML html(data);

    std::optional<Product> product = store->GetProductAtURL(html);
    if (!product) return { ResultType::GENERIC_ERROR, nullptr };

    return { ResultType::GENERIC_VALID, new Product(std::move(*product)) };
}

static Result TC_GetProduct_Fetch(TaskContext ctx, App* app, const std::string& url,
    const Store* store)
{
    auto handle = ctx.delegator.QueueExtraExternalTask(ctx.group_id);
    app->curl_driver->PerformTransfer(url, [handle, ctx, store] (auto data, auto url,
        CURLcode code) {
        if (code == CURLE_OK) {
            ctx.delegator.QueueExtraTasks(ctx.group_id, tb::make_span({
                Task { TC_GetProduct_Parse, store, std::string { data } }
            }));
        } else {
            handle.Finish({ ResultType::GENERIC_ERROR, nullptr });
        }
        handle.Finish({});
    });

    return {};
}

// Product list
static Result TC_DoQuery_Parse(TaskContext ctx, const Store* store,
    const std::string& data, size_t depth)
{
    ProductList list = store->ParseProductSearch(data, depth);

    return {
        ResultType::GENERIC_VALID,
        new std::pair<bool, ProductList>(true, std::move(list))
    };
}

static Result TC_DoQuery(TaskContext ctx, App* app, const std::string& query_string,
    const StoreSelection& stores, size_t depth)
{
    // Figure out what pages need to be fetched first
    for (StoreID id : stores) {
        const Store* store = app->GetStore(id);
        if (store == nullptr) {
            Log(LogLevel::WARNING, "Invalid store ID {}", static_cast<int>(id));
            continue;
        }
        std::string url = store->GetProductSearchURL(query_string);
        CURLOptions request_options = store->GetProductSearchCURLOptions(query_string);

        auto handle = ctx.delegator.QueueExtraExternalTask(ctx.group_id);

        app->curl_driver->PerformTransfer(url,
        [handle, ctx, store, depth] (auto data, auto url, CURLcode code) {
            ctx.delegator.QueueExtraTasks(ctx.group_id, tb::make_span({
                Task { TC_DoQuery_Parse, store, std::string { data }, depth }
            }));
            handle.Finish({});
        }, request_options);
    }

    return {};
}

static Result TC_GetQueriesDB(TaskContext ctx, App* app,
    const std::string& query_string, const StoreSelection& stores, size_t depth)
{
    // Get query template stored in database
    ProductList list(depth);
    auto qt = app->database.GetQueryTemplates(tb::make_span({ query_string }));

    StoreSelection missing;
    // Check if query is "complete" i.e. has the information we're looking for
    if (qt.empty()) {
        // If no query found, perform queries on all stores
        missing = stores;
    } else {
        // If query found, check to see if all stores contained & deep enough
        const QueryTemplate& q = qt[0];
        std::time_t now = std::time(nullptr);
        if (q.depth < depth || now - q.timestamp > ENTRY_EXPIRY_TIME_SECONDS) {
            // Query not deep enough or expired, redo all stores
            missing = stores;
        } else {
            // Are all the stores that we asked for there?
            if (!std::ranges::includes(q.stores, stores)) {
                missing = stores;
                // Just redo the missing ones
                for (StoreID id : q.stores) std::erase(missing, id);
            }

            // Retrieve products from database as well
            auto relevant_ids = std::views::keys(
                q.results | std::views::filter([depth] (auto& pair) {
                    auto& [id, info] = pair;
                    return info.relevance < depth;
                })
            ) | tb::range_to<std::vector<std::string>>();
            auto products = app->database.GetProducts(relevant_ids);

            for (auto& p : products)
                list.products.emplace_back(std::move(p), q.results.at(p.id));
        }
    }

    if (missing.size()) {
        // Queue tasks to retrieve missing info
        ctx.delegator.QueueExtraTasks(ctx.group_id, tb::make_span({
            Task { TC_DoQuery, app, query_string, missing, depth }
        }));
    }

    return {
        ResultType::GENERIC_VALID,
        new std::pair<bool, ProductList>(false, std::move(list))
    };
}

// App

App::App(std::string_view cfg_path)
{
    CURLDriver::GlobalInit();
    buxtehude::Initialise([] (buxtehude::LogLevel l, std::string_view msg) {
        Log(static_cast<LogLevel>(l), "(buxtehude) {}", msg);
    });

    Log(LogLevel::INFO, "Starting Fitsch {}", FITSCH_VERSION);

    std::ifstream cfg_file(cfg_path);

    if (!cfg_file.is_open()) {
        Log(LogLevel::SEVERE, "Failed to open `config.json`. Shutting down");
        std::exit(1);
    }

    json config = json::parse(cfg_file);
    cfg_file.close();

    std::string mongouri = config["mongodb_uri"];
    std::string user_agent = config["curl"]["user_agent"];

    curl_driver = std::make_unique<CURLDriver>(128, user_agent);
    curl_driver->Run();

    database.Connect({mongouri});

    bclient = std::make_unique<buxtehude::Client>(buxtehude::ClientPreferences {
        .format = buxtehude::MessageFormat::MSGPACK,
        .teamname = "webscraper"
    });

    if (auto connect_err = bclient->IPConnect("localhost", 1637);
        connect_err.is_error()) {
        Log(LogLevel::WARNING, "Failed to connect to buxtehude server");
        bclient->Close();
        bclient.reset();
        return;
    }

    Log(LogLevel::INFO, "Established connection to buxtehude server");

    bclient->AddHandler("query", [this] (buxtehude::Client& client,
                        const buxtehude::Message& msg) {
        if (!buxtehude::ValidateJSON(msg.content, validate::QUERY)) return;
        int request_id = msg.content["request-id"];
        size_t depth = msg.content["depth"];
        StoreSelection stores = msg.content["stores"];

        for (const json& j : msg.content["terms"]) {
            auto term = j.get<std::string>();
            delegator.QueueTasks(
                { SendQuery, this, msg.src, term, stores, request_id },
                tb::make_span({
                    Task { TC_GetQueriesDB, this, term, stores, depth }
                })
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

const Store* App::GetStore(StoreID id)
{
    if (!stores.contains(id)) return nullptr;
    return stores[id];
}

void App::GetProductAtURL(StoreID store, std::string_view item_url)
{
    if (!stores.contains(store)) {
        Log(LogLevel::WARNING, "Invalid store!");
        return;
    }

    delegator.QueueTasks(
        { PrintProduct, this, std::string { item_url } },
        tb::make_span({
            Task { TC_GetProduct_Fetch, this, std::string { item_url }, stores[store] }
        })
    );
}
