#pragma once

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

#define VERSION "0.0.1"
#define ENTRY_EXPIRY_TIME 86400 * 2

using std::string;

class App
{
public:
    App(std::string_view cfg_path);
    ~App();

    void AddStore(const Store* store);
    const Store* GetStore(StoreID id);

    void GetProductAtURL(StoreID store, const string& item_url);

    Delegator delegator{16};
    std::unique_ptr<CURLDriver> curl_driver;
    Database database;
    std::unique_ptr<buxtehude::Client> bclient;

private:
    std::map<StoreID, const Store*> stores;
};
