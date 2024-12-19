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

    ProductList (*ParseProductSearch)(const string&, int);
    string (*GetProductSearchURL)(std::string_view);
    Product (*GetProductAtURL)(const HTML&);
};

// See stores.md
ProductList SV_ParseProductSearch(const string& data, int depth=0);
string SV_GetProductSearchURL(std::string_view query);
Product SV_GetProductAtURL(const HTML& html);

namespace stores
{

inline const Store SuperValu =
{
    .id = SUPERVALU, .name = "SuperValu", .prefix = "SV",
    .homepage = "https://shop.supervalu.ie/sm/delivery/rsid/5550",
    .region = IE,
    .GetProductSearchURL = SV_GetProductSearchURL,
    .GetProductAtURL = SV_GetProductAtURL,
    .ParseProductSearch = SV_ParseProductSearch
};

}
