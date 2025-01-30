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
    string name, prefix, homepage;
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

inline const Store Tesco = {
    .id = TESCO, .name = "Tesco", .prefix = "TE",
    .homepage = "https://www.tesco.ie/groceries/en-IE",
    .region = IE,
    .GetProductSearchURL = TE_GetProductSearchURL,
    .GetProductAtURL = TE_GetProductAtURL,
    .ParseProductSearch = TE_ParseProductSearch
};

}
