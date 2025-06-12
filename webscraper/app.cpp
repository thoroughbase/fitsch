#include "webscraper/app.hpp"

#include <fstream>
#include <cstdlib>
#include <ranges>

#include <fmt/format.h>
#include <fmt/core.h>

#include <nlohmann/json.hpp>

#include <buxtehude/tb.hpp>
#include <buxtehude/validate.hpp>

#include "common/product.hpp"
#include "common/util.hpp"
#include "common/validate.hpp"

#include <chrono>

constexpr std::string_view PRODUCTS_DATABASE = "products";
constexpr std::string_view QUERIES_DATABASE = "queries";

constexpr auto DATABASE_UPLOAD_FAILED = [] (dflat::DatabaseError) {
    Log(LogLevel::WARNING, "Failed to upload to database!");
};

constexpr auto DATABASE_GET_FAILED = [] (dflat::DatabaseError) {
    Log(LogLevel::WARNING, "Failed to get documents from database!");
};

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

    app->db_handle.Put(PRODUCTS_DATABASE, product.id, product, true)
        .if_err(DATABASE_UPLOAD_FAILED);
}

static void SendQuery(const std::vector<Result>& results, App* app,
    const std::string& dest, const std::string& query_string,
    StoreSelection stores, unsigned request_id)
{
    ProductList list;
    bool upload = false;

    for (const Result& result : results) {
        if (result.Type() != ResultType::GENERIC_VALID) continue;
        auto& [queried_website, product_list]
            = result.Get<std::pair<bool, ProductList>>();

        list.Add(product_list);
        if (queried_website) upload = true;
    }

    std::vector<Product> products = list.AsProductVector();

    app->bclient.Write({ .dest = dest, .type = "query-result",
        .content = {
            { "items", products },
            { "term", query_string },
            { "request-id", request_id }
        }
    }).if_err([] (bux::WriteError) {
        Log(LogLevel::WARNING, "Failed to write back query-result - connection closed");
    });

    if (upload) {
        Log(LogLevel::DEBUG, "Uploading query {}", query_string);
        app->db_handle.Put(QUERIES_DATABASE, query_string,
            list.AsQueryTemplate(query_string, stores), true)
            .if_err(DATABASE_UPLOAD_FAILED);

        if (!list.products.empty()) {
            auto product_pairs = products
            | std::views::transform([] (const Product& p)
                -> std::tuple<const std::string&, const Product&> {
                return { p.id, p };
            });

            app->db_handle.PutMany<Product>(PRODUCTS_DATABASE, product_pairs, true)
                .if_err(DATABASE_UPLOAD_FAILED);
        }
    }
}

static Result TC_DoQuery(TaskContext ctx, App* app, const std::string& query_string,
    StoreSelection stores, size_t depth)
{
    for (StoreID id : stores) {
        const Store* store = app->GetStore(id);
        if (store == nullptr) {
            Log(LogLevel::WARNING, "Invalid store ID {}", static_cast<int>(id));
            continue;
        }
        std::string url = store->GetProductSearchURL(query_string);
        CURLOptions request_options = store->GetProductSearchCURLOptions(query_string);

        ExternalTaskHandle handle = ctx.delegator.QueueExtraExternalTask(ctx.group_id);

        app->curl_driver.PerformTransfer(url,
        [handle, store, depth] (auto data, auto url, CURLcode code) {
            if (code == CURLE_OK) {
                ProductList list = store->ParseProductSearch(data, depth);
                handle.Finish({
                    ResultType::GENERIC_VALID,
                    new std::pair<bool, ProductList>(true, std::move(list))
                });
            } else {
                handle.Finish({ ResultType::GENERIC_ERROR, nullptr });
            }
        }, request_options);
    }

    return {};
}

static Result TC_GetQueriesDB(TaskContext ctx, App* app,
    const std::string& query_string, StoreSelection stores, size_t depth,
    bool force_refresh)
{
    ProductList list(depth);
    StoreSelection missing = stores;

    if (!force_refresh) {
        app->db_handle.Get<QueryTemplate>(QUERIES_DATABASE, query_string)
        .if_err([] (dflat::DatabaseError e) {
            if (e != dflat::DatabaseError::KEY_NOT_FOUND)
                DATABASE_GET_FAILED(e);
        }).if_ok([&] (const QueryTemplate& query_info) {
            std::time_t time_elapsed = std::time(nullptr) - query_info.timestamp;
            if (query_info.depth < depth
                || time_elapsed > app->config.entry_expiry_time_seconds) {
                return;
            }

            auto relevant_ids = std::views::keys(
                query_info.results | std::views::filter([depth] (auto& pair) {
                    auto& [id, info] = pair;
                    return info.relevance < depth;
                })
            ) | tb::range_to<std::vector<std::string_view>>();

            app->db_handle.GetMany<Product>(PRODUCTS_DATABASE, relevant_ids)
            .if_ok_mut([&] (std::unordered_map<std::string, Product>& results) {
                if (results.size() == relevant_ids.size()) {
                    missing = stores.without(query_info.stores);

                    for (auto& [id, product] : results)
                        list.products.emplace_back(std::move(product),
                            query_info.results.at(id));
                }
            })
            .if_err(DATABASE_GET_FAILED);
        });
    }

    if (missing) {
        ctx.delegator.QueueExtraTasks(ctx.group_id, tb::make_span({
            Task { TC_DoQuery, app, query_string, missing, depth }
        }));
    }

    return {
        ResultType::GENERIC_VALID,
        new std::pair<bool, ProductList>(false, std::move(list))
    };
}

// AppConfig

std::optional<AppConfig> AppConfig::FromJSONFile(std::string_view path)
{
    AppConfig result;

    std::ifstream cfg_file(path.data());

    if (!cfg_file.is_open()) {
        Log(LogLevel::WARNING, "Failed to open config file '{}'", path);
        return {};
    }

    json cfg_json;
    try {
        cfg_json = json::parse(cfg_file);
    } catch (const json::parse_error& e) {
        Log(LogLevel::WARNING, "Failed to parse config JSON: {}", e.what());
        return {};
    }

    if (cfg_json.contains("/dflat-db-name"_json_pointer)) {
        cfg_json["dflat-db-name"].get_to(result.dflat_db_name);
    }

    if (cfg_json.contains("/curl/user-agent"_json_pointer)) {
        cfg_json["curl"]["user-agent"].get_to(result.curl_useragent);
    }

    if (cfg_json.contains("/buxtehude/type"_json_pointer)) {
        const json& type = cfg_json["buxtehude"]["type"];
        if (type == "unix")
            result.bux_conn_type = bux::ConnectionType::UNIX;
    }

    if (cfg_json.contains("/buxtehude/path-or-hostname"_json_pointer)) {
        cfg_json["buxtehude"]["path-or-hostname"].get_to(result.bux_path_or_hostname);
    }

    if (cfg_json.contains("/buxtehude/port"_json_pointer)) {
        const json& port = cfg_json["buxtehude"]["port"];
        if (port.is_number())
            result.bux_port = port;
    }

    if (cfg_json.contains("/entry-expiry-time-seconds"_json_pointer)) {
        const json& time = cfg_json["entry-expiry-time-seconds"];
        if (time.is_number())
            result.entry_expiry_time_seconds = time;
    }

    if (cfg_json.contains("/max-concurrent-transfers"_json_pointer)) {
        const json& max_transfers = cfg_json["max-concurrent-transfers"];
        if (max_transfers.is_number())
            result.max_concurrent_transfers = max_transfers.get<unsigned>();
    }

    return result;
}

void App::RetryConnection()
{
    using namespace std::chrono_literals;

    constexpr static auto BASE_WAIT_TIME = 5s;
    constexpr static auto MAX_WAIT_TIME = 40s;
    static auto wait_time = BASE_WAIT_TIME;

    std::thread reconnect_thread([this] {
        std::this_thread::sleep_for(wait_time);
        BuxConnect().if_err([this] (bux::ConnectError e) {
            Log(LogLevel::WARNING, "Failed to connect to buxtehude server: {}",
                e.What());
            if (wait_time < MAX_WAIT_TIME) wait_time += 5s;
            RetryConnection();
        }).if_ok([] {
            wait_time = BASE_WAIT_TIME;
            Log(LogLevel::INFO, "Reconnected to buxtehude server");
        });
    });
    reconnect_thread.detach();
}

// App

App::App(AppConfig& cfg_temp) : config(std::move(cfg_temp))
{
    CURLDriver::GlobalInit();
    bux::Initialise([] (bux::LogLevel level, std::string_view msg) {
        if (level < bux::LogLevel::SEVERE) return;
        Log(static_cast<LogLevel>(level), "(buxtehude) {}", msg);
    });

    curl_driver.Init(config.max_concurrent_transfers, config.curl_useragent);

    db_handle.server_name = config.dflat_db_name;

    bclient.preferences = {
        .teamname = "webscraper",
        .format = bux::MessageFormat::MSGPACK
    };

    bclient.AddHandler("query", [this] (bux::Client& client, const bux::Message& msg) {
        if (!bux::ValidateJSON(msg.content, validate::QUERY)) return;
        unsigned request_id = msg.content["request-id"];
        size_t depth = msg.content["depth"];
        auto stores = msg.content["stores"].get<StoreSelection>();
        bool force_refresh = msg.content["force-refresh"];

        for (const json& j : msg.content["terms"]) {
            auto term = j.get<std::string>();
            delegator.QueueTasks(
                { SendQuery, this, msg.src, term, stores, request_id },
                tb::make_span({
                    Task { TC_GetQueriesDB, this, term, stores, depth, force_refresh }
                })
            );
        }
    });

    bclient.SetDisconnectHandler([this] (bux::Client& client) {
        Log(LogLevel::WARNING, "Connection dropped to buxtehude server, retrying...");
        RetryConnection();
    });

    BuxConnect().if_err([this] (bux::ConnectError e) {
        Log(LogLevel::WARNING, "Failed to connect to buxtehude server: {}, retrying...",
            e.What());
        RetryConnection();
    }).if_ok([] {
        Log(LogLevel::INFO, "Established connection to buxtehude server");
    });
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

void App::GetProductAtURL(StoreID store_id, std::string_view item_url)
{
    const Store* store = GetStore(store_id);
    if (store == nullptr) {
        Log(LogLevel::WARNING, "Invalid store!");
        return;
    }

    ExternalTaskHandle handle = delegator.QueueExternalTask({
        PrintProduct, this, std::string { item_url }
    });

    curl_driver.PerformTransfer(item_url, [handle, store] (auto data, auto url,
        CURLcode code) {
        if (code == CURLE_OK) {
            std::optional<HTML> html = HTML::FromString(data);
            if (!html) {
                handle.Finish({ ResultType::GENERIC_ERROR, nullptr });
                return;
            }

            std::optional<Product> product = store->GetProductAtURL(html.value());
            if (!product) {
                handle.Finish({ ResultType::GENERIC_ERROR, nullptr });
                return;
            }

            handle.Finish({
                ResultType::GENERIC_VALID,
                new Product(std::move(*product))
            });
        } else {
            handle.Finish({ ResultType::GENERIC_ERROR, nullptr });
        }
    });
}

bux::tb::error<bux::ConnectError> App::BuxConnect()
{
    switch (config.bux_conn_type) {
    case bux::ConnectionType::INTERNET:
        return bclient.IPConnect(config.bux_path_or_hostname, config.bux_port);
    case bux::ConnectionType::UNIX:
        return bclient.UnixConnect(config.bux_path_or_hostname);
    case bux::ConnectionType::INTERNAL:
        assert(config.bux_conn_type != bux::ConnectionType::INTERNAL);
        break;
    }

    return bux::tb::ok;
}
