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

#include <chrono>

constexpr std::string_view PRODUCTS_DATABASE = "products";
constexpr std::string_view QUERIES_DATABASE = "queries";

constexpr auto DATABASE_UPLOAD_FAILED = [] (dflat::DatabaseError) {
    Log(LogLevel::WARNING, "Failed to upload to database!");
};

constexpr auto DATABASE_GET_FAILED = [] (dflat::DatabaseError) {
    Log(LogLevel::WARNING, "Failed to get documents from database!");
};

auto RETRY_TASK = [] (auto) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(5ms);
};

// ResultCallbacks

static void PrintProduct(GroupHandle, std::span<Result> results, App* app,
    std::string_view url)
{
    if (results.empty()) {
        Log(LogLevel::WARNING, "No product found at URL {}", url);
        return;
    }

    if (results[0].GetType() == Result::GENERIC_ERROR) {
        Log(LogLevel::WARNING, "Error whilst fetching/parsing product at URL {}", url);
        return;
    }

    auto& product = results[0].Get<ArenaProduct>();

    fmt::print("Product at URL `{}`:\n  {}: {} [{}]\n", url, product.name,
               product.item_price.ToString(), product.price_per_unit.ToString());

    app->db_handle.Put(PRODUCTS_DATABASE, product.id, product, true)
        .if_err(DATABASE_UPLOAD_FAILED);
}

static void SendQuery(GroupHandle g, std::span<Result> results, App* app,
    std::string_view dest, std::string_view query_string,
    StoreSelection stores, unsigned request_id)
{
    bool upload = false;
    json items_to_send = json::array();
    tb::arena_vector<std::pair<std::string_view, PMRProduct&>> product_pairs {
        g.group->results_region
    };

    tb::scoped_guard free_pmr_products = [&product_pairs] () {
        for (auto& [_, product] : product_pairs) {
            if (std::holds_alternative<Product>(product))
                std::destroy_at(&product);
        }
    };

    ArenaQueryTemplate qt {
        .query_string { query_string, g.group->results_region },
        .stores = stores,
        .results { g.group->results_region },
        .timestamp = std::time(nullptr),
        .depth = SEARCH_DEPTH_INDEFINITE
    };

    for (const Result& result : results) {
        if (result.GetType() == Result::EMPTY)
            continue;

        if (result.GetType() != Result::GENERIC_VALID) {
            stores = stores.without(result.Get<StoreID>());
            continue;
        }

        auto& [queried_website, product_list]
            = result.Get<std::pair<bool, ArenaProductList*>>();

        upload |= queried_website;

        if (product_list == nullptr)
            continue;

        if (product_list->depth < qt.depth)
            qt.depth = product_list->depth;

        for (const auto& [product, result_info] : product_list->products) {
            items_to_send.push_back(product);
            auto id = std::visit([] (const auto& p) -> std::string_view {
                return p.id;
            }, product);

            product_pairs.emplace_back(id, product);
            qt.results.emplace(id, result_info);
        }
    }

    {
        std::scoped_lock client_lock { app->client_mutex };
        app->bclient.Write({ .dest { dest }, .type = "query-result",
            .content = {
                { "items", std::move(items_to_send) },
                { "term", query_string },
                { "request-id", request_id }
            }
        }).if_err([] (bux::WriteError) {
            Log(LogLevel::WARNING,
                "Failed to write back query-result - connection closed");
        });
    }

    if (!upload)
        return;

    Log(LogLevel::DEBUG, "Uploading query {}", query_string);
    app->db_handle.Put(QUERIES_DATABASE, query_string, qt, true)
        .if_err(DATABASE_UPLOAD_FAILED);

    if (!product_pairs.empty()) {
        app->db_handle.PutMany<PMRProduct>(PRODUCTS_DATABASE, product_pairs, true)
            .if_err(DATABASE_UPLOAD_FAILED);
    }
}

static Result TC_DoQuery(GroupHandle group, App* app, std::string_view query_string,
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

        auto transfer_task = group.CreateExternalTask();
        group.QueueTasks({}, { transfer_task }).ignore_error();

        app->curl_driver.PerformTransfer(url,
        [transfer_task, store, depth, id, group] (auto data, auto url, CURLcode code) {
            if (code == CURLE_OK) {
                transfer_task.PushResult({
                    group.AllocateResult<std::pair<bool, ArenaProductList*>>(
                        true,
                        store->ParseProductSearch(
                            data, group.group->results_region, depth
                        )
                    ),
                    Result::GENERIC_VALID
                });
            } else {
                transfer_task.PushResult({
                    group.AllocateResult<StoreID>(id),
                    Result::GENERIC_ERROR
                });
            }
        }, request_options);
    }

    return {};
}

static Result TC_GetQueriesDB(GroupHandle group, App* app,
    std::string_view query_string, StoreSelection stores, size_t depth,
    bool force_refresh)
{
    auto& list = *group.AllocateResult<ArenaProductList>(
        ArenaProductList::WithArena(group.group->results_region)
    );
    list.depth = depth;
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

            size_t ids_count = 0;
            auto relevant_ids = std::views::keys(
                query_info.results | std::views::filter([depth, &ids_count] (auto& pair) {
                    auto& [id, info] = pair;
                    ++ids_count;
                    return info.relevance < depth;
                })
            );

            app->db_handle.GetMany<Product>(PRODUCTS_DATABASE, relevant_ids)
            .if_ok_mut([&] (std::unordered_map<std::string, Product>& results) {
                if (results.size() != ids_count)
                    return;

                missing = stores.without(query_info.stores);

                for (auto& [id, product] : results) {
                    auto& product_copy = *group.AllocateResult<PMRProduct>(
                        std::move(product)
                    );
                    list.products.emplace_back(
                        product_copy,
                        query_info.results.at(id)
                    );
                }
            })
            .if_err(DATABASE_GET_FAILED);
        });
    }

    if (missing) {
        Task do_query { TC_DoQuery, app, query_string, missing, depth };
        group.QueueTasks(tb::make_span({ do_query })).ignore_error();
    }

    return {
        group.AllocateResult<std::pair<bool, ArenaProductList*>>(
            false, &list
        ),
        Result::GENERIC_VALID
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

static void Bux_HandleQuery(bux::Client& client, const bux::Message& msg, App* app)
{
    if (!bux::ValidateJSON(msg.content, validate::QUERY)) return;

    unsigned request_id = msg.content["request-id"];
    size_t depth = msg.content["depth"];
    auto stores = msg.content["stores"].get<StoreSelection>();
    bool force_refresh = msg.content["force-refresh"];

    for (const json& term_obj : msg.content["terms"]) {
        GroupHandle group;
        while (
            app->delegator.NewTaskGroup()
            .try_move(group)
            .if_err(RETRY_TASK)
            .is_error()
        ) {}

        std::string_view term {
            *group.AllocateArg<tb::arena_string>(term_obj.get<std::string_view>())
        };

        std::string_view message_source {
            *group.AllocateArg<tb::arena_string>(msg.src)
        };

        group.SetResultCallback({
            SendQuery, app, message_source, term, stores, request_id
        });

        bool reattempt = false;
        while (group.QueueTasks(
            tb::make_span({
                Task { TC_GetQueriesDB, app, term, stores, depth, force_refresh }
            }),
            {},
            reattempt
        ).if_err(RETRY_TASK).is_error()) {
            reattempt = true;
        }
    }
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
        Bux_HandleQuery(client, msg, this);
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

    GroupHandle group;
    while (
        delegator.NewTaskGroup()
        .try_move(group)
        .if_err(RETRY_TASK)
        .is_error()
    ) {}

    std::string_view url_arg {
        *group.AllocateArg<tb::arena_string>(item_url)
    };

    group.SetResultCallback({ PrintProduct, this, url_arg });

    auto transfer_task = group.CreateExternalTask();
    group.QueueTasks({}, { transfer_task }).ignore_error();

    curl_driver.PerformTransfer(item_url,
        [group, transfer_task, store] (auto data, auto url, CURLcode code) {
        if (code == CURLE_OK) {
            std::optional<HTML> html = HTML::FromString(data);
            if (!html) {
                transfer_task.PushResult(Result::Error());
                return;
            }

            ArenaProduct* product = store->GetProductAtURL(html.value(), group.group->results_region);
            if (product == nullptr) {
                transfer_task.PushResult(Result::Error());
                return;
            }

            transfer_task.PushResult({
                product,
                Result::GENERIC_VALID
            });
        } else {
            transfer_task.PushResult(Result::Error());
        }
    });
}

tb::error<bux::ConnectError> App::BuxConnect()
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

    return tb::ok;
}
