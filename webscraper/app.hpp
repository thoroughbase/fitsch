#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <memory>

#include <curl/curl.h>

#include "common/product.hpp"
#include "webscraper/stores.hpp"
#include "webscraper/curldriver.hpp"
#include "webscraper/task.hpp"

#include <buxtehude/buxtehude.hpp>
#include <dflat/dflat.hpp>

constexpr std::string_view FITSCH_VERSION = "0.0.1";
constexpr std::time_t DEFAULT_ENTRY_EXPIRY_TIME_SECONDS = 86400 * 2;

namespace bux = buxtehude;

struct AppConfig
{
    std::string dflat_db_name = "dflat";
    std::string curl_useragent = "Mozilla/5.0";
    std::string bux_path_or_hostname = "localhost";
    std::time_t entry_expiry_time_seconds = DEFAULT_ENTRY_EXPIRY_TIME_SECONDS;
    bux::ConnectionType bux_conn_type = bux::ConnectionType::INTERNET;
    unsigned max_concurrent_transfers = 32;
    uint16_t bux_port = bux::DEFAULT_PORT;

    static std::optional<AppConfig> FromJSONFile(std::string_view path);
};

class App
{
public:
    App(AppConfig& config);
    ~App();

    void AddStore(const Store* store);
    const Store* GetStore(StoreID id);

    void GetProductAtURL(StoreID store, std::string_view item_url);

    Delegator delegator {16};
    CURLDriver curl_driver;
    bux::Client bclient;
    AppConfig config;
    dflat::Handle db_handle { bclient };

private:
    void RetryConnection();
    bux::tb::error<bux::ConnectError> BuxConnect();

    std::unordered_map<StoreID, const Store*> stores;
};
