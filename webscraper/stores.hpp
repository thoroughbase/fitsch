#pragma once

#include <string>
#include <vector>

#include "common/product.hpp"
#include "webscraper/html.hpp"

using std::string;

struct Store
{
    StoreID id;
    string name, prefix, homepage;
    Region region;

    ProductList (*SearchProducts)(const string&, CURL*, int);
    Product (*GetProductAtURL)(const HTML&);
};

// See stores.md
ProductList SV_Search(const string& query, CURL* curl, int depth=0);
Product SV_GetProductAtURL(const HTML& html);

namespace stores
{

inline const Store SuperValu =
{
    .id = SUPERVALU, .name = "SuperValu", .prefix = "SV",
    .homepage = "https://shop.supervalu.ie/sm/delivery/rsid/5550/",
    .region = IE,
    .SearchProducts = SV_Search,
    .GetProductAtURL = SV_GetProductAtURL
};

}
