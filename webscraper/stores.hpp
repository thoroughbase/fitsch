#pragma once

#include <string>
#include <string_view>
#include <optional>

#include "common/product.hpp"
#include "webscraper/curldriver.hpp"
#include "webscraper/html.hpp"

struct Store
{
    StoreID id;
    std::string_view name, prefix, homepage, root_url;
    Region region;

    ProductList (*ParseProductSearch)(std::string_view, int);
    std::string (*GetProductSearchURL)(std::string_view);
    std::optional<Product> (*GetProductAtURL)(const HTML&);
    CURLOptions (*GetProductSearchCURLOptions)(std::string_view);
};

// See stores.md

CURLOptions Default_GetProductSearchCURLOptions(std::string_view query);

// SuperValu
ProductList SV_ParseProductSearch(std::string_view data, int depth=0);
std::string SV_GetProductSearchURL(std::string_view query);
std::optional<Product> SV_GetProductAtURL(const HTML& html);

// Tesco
ProductList TE_ParseProductSearch(std::string_view data, int depth=0);
std::string TE_GetProductSearchURL(std::string_view query);
std::optional<Product> TE_GetProductAtURL(const HTML& html);

// Dunnes Stores
ProductList DS_ParseProductSearch(std::string_view data, int depth=0);
std::string DS_GetProductSearchURL(std::string_view query);
std::optional<Product> DS_GetProductAtURL(const HTML& html);

// Aldi
ProductList AL_ParseProductSearch(std::string_view data, int depth=0);
std::string AL_GetProductSearchURL(std::string_view query);
std::optional<Product> AL_GetProductAtURL(const HTML& html);
CURLOptions AL_GetProductSearchCURLOptions(std::string_view query);

namespace stores
{

constexpr Store SuperValu =
{
    .id = StoreID::SUPERVALU, .name = "SuperValu", .prefix = "SV",
    .homepage = "https://shop.supervalu.ie/sm/delivery/rsid/5550",
    .root_url = "https://shop.supervalu.ie",
    .region = Region::IE,
    .GetProductSearchURL = SV_GetProductSearchURL,
    .GetProductAtURL = SV_GetProductAtURL,
    .ParseProductSearch = SV_ParseProductSearch,
    .GetProductSearchCURLOptions = Default_GetProductSearchCURLOptions
};

constexpr Store Tesco = {
    .id = StoreID::TESCO, .name = "Tesco", .prefix = "TE",
    .homepage = "https://www.tesco.ie/groceries/en-IE",
    .root_url = "https://www.tesco.ie",
    .region = Region::IE,
    .GetProductSearchURL = TE_GetProductSearchURL,
    .GetProductAtURL = TE_GetProductAtURL,
    .ParseProductSearch = TE_ParseProductSearch,
    .GetProductSearchCURLOptions = Default_GetProductSearchCURLOptions
};

constexpr Store DunnesStores = {
    .id = StoreID::DUNNES_STORES, .name = "Dunnes Stores", .prefix = "DS",
    .homepage = "https://www.dunnesstoresgrocery.com",
    .root_url = "https://www.dunnesstoresgrocery.com",
    .region = Region::IE,
    .GetProductSearchURL = DS_GetProductSearchURL,
    .GetProductAtURL = DS_GetProductAtURL,
    .ParseProductSearch = DS_ParseProductSearch,
    .GetProductSearchCURLOptions = Default_GetProductSearchCURLOptions
};

constexpr Store Aldi = {
    .id = StoreID::ALDI, .name = "Aldi", .prefix = "AL",
    .homepage = "https://groceries.aldi.ie",
    .root_url = "https://groceries.aldi.ie",
    .region = Region::IE,
    .GetProductSearchURL = AL_GetProductSearchURL,
    .GetProductAtURL = AL_GetProductAtURL,
    .ParseProductSearch = AL_ParseProductSearch,
    .GetProductSearchCURLOptions = AL_GetProductSearchCURLOptions
};

}
