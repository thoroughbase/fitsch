#pragma once

#include <string>
#include <string_view>
#include <optional>

#include "common/product.hpp"
#include "webscraper/html.hpp"

using std::string;

struct Store
{
    StoreID id;
    std::string_view name, prefix, homepage, root_url;
    Region region;

    ProductList (*ParseProductSearch)(std::string_view, int);
    string (*GetProductSearchURL)(std::string_view);
    std::optional<Product> (*GetProductAtURL)(const HTML&);
};

// See stores.md
// SuperValu
ProductList SV_ParseProductSearch(std::string_view data, int depth=0);
string SV_GetProductSearchURL(std::string_view query);
std::optional<Product> SV_GetProductAtURL(const HTML& html);

// Tesco
ProductList TE_ParseProductSearch(std::string_view data, int depth=0);
string TE_GetProductSearchURL(std::string_view query);
std::optional<Product> TE_GetProductAtURL(const HTML& html);

// Dunnes Stores
ProductList DS_ParseProductSearch(std::string_view data, int depth=0);
string DS_GetProductSearchURL(std::string_view query);
std::optional<Product> DS_GetProductAtURL(const HTML& html);

namespace stores
{

constexpr Store SuperValu =
{
    .id = SUPERVALU, .name = "SuperValu", .prefix = "SV",
    .homepage = "https://shop.supervalu.ie/sm/delivery/rsid/5550",
    .root_url = "https://shop.supervalu.ie",
    .region = IE,
    .GetProductSearchURL = SV_GetProductSearchURL,
    .GetProductAtURL = SV_GetProductAtURL,
    .ParseProductSearch = SV_ParseProductSearch
};

constexpr Store Tesco = {
    .id = TESCO, .name = "Tesco", .prefix = "TE",
    .homepage = "https://www.tesco.ie/groceries/en-IE",
    .root_url = "https://www.tesco.ie",
    .region = IE,
    .GetProductSearchURL = TE_GetProductSearchURL,
    .GetProductAtURL = TE_GetProductAtURL,
    .ParseProductSearch = TE_ParseProductSearch
};

constexpr Store DunnesStores = {
    .id = DUNNES_STORES, .name = "Dunnes Stores", .prefix = "DS",
    .homepage = "https://www.dunnesstoresgrocery.com",
    .root_url = "https://www.dunnesstoresgrocery.com",
    .region = IE,
    .GetProductSearchURL = DS_GetProductSearchURL,
    .GetProductAtURL = DS_GetProductAtURL,
    .ParseProductSearch = DS_ParseProductSearch
};

}
