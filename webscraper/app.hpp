#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <memory>

#include <curl/curl.h>

#include "common/product.hpp"
#include "webscraper/stores.hpp"
#include "webscraper/database.hpp"
#include "webscraper/curldriver.hpp"
#include "webscraper/task.hpp"

#include <buxtehude/buxtehude.hpp>

constexpr std::string_view FITSCH_VERSION = "0.0.1";
constexpr std::time_t ENTRY_EXPIRY_TIME_SECONDS = 86400 * 2;

namespace bux = buxtehude;

struct AppConfig
{
    std::string mongodb_uri;
    std::string curl_useragent;

    static std::optional<AppConfig> FromJSONFile(std::string_view path);
};

class App
{
public:
    App(std::string_view cfg_path);
    ~App();

    void AddStore(const Store* store);
    const Store* GetStore(StoreID id);

    void GetProductAtURL(StoreID store, std::string_view item_url);

    Delegator delegator{16};
    std::unique_ptr<CURLDriver> curl_driver;
    Database database;
    std::unique_ptr<bux::Client> bclient;

private:
    std::map<StoreID, const Store*> stores;
};
