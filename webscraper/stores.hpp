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

    ArenaProductList* (*ParseProductSearch)(std::string_view,
        tb::thread_safe_memory_arena& arena, size_t);
    std::string (*GetProductSearchURL)(std::string_view);
    ArenaProduct* (*GetProductAtURL)(const HTML&, tb::thread_safe_memory_arena& arena);
    CURLOptions (*GetProductSearchCURLOptions)(std::string_view);
};

// See stores.md

CURLOptions Default_GetProductSearchCURLOptions(std::string_view query);

// SuperValu
ArenaProductList* SV_ParseProductSearch(std::string_view data,
    tb::thread_safe_memory_arena& arena,
    size_t depth=SEARCH_DEPTH_INDEFINITE);
std::string SV_GetProductSearchURL(std::string_view query);
ArenaProduct* SV_GetProductAtURL(const HTML& html, tb::thread_safe_memory_arena& arena);

// Tesco
ArenaProductList* TE_ParseProductSearch(std::string_view data,
    tb::thread_safe_memory_arena& arena,
    size_t depth=SEARCH_DEPTH_INDEFINITE);
std::string TE_GetProductSearchURL(std::string_view query);
ArenaProduct* TE_GetProductAtURL(const HTML& html, tb::thread_safe_memory_arena& arena);

// Dunnes Stores
ArenaProductList* DS_ParseProductSearch(std::string_view data,
    tb::thread_safe_memory_arena& arena,
    size_t depth=SEARCH_DEPTH_INDEFINITE);
std::string DS_GetProductSearchURL(std::string_view query);
ArenaProduct* DS_GetProductAtURL(const HTML& html, tb::thread_safe_memory_arena& arena);

// Aldi
ArenaProductList* AL_ParseProductSearch(std::string_view data,
    tb::thread_safe_memory_arena& arena,
    size_t depth=SEARCH_DEPTH_INDEFINITE);
std::string AL_GetProductSearchURL(std::string_view query);
ArenaProduct* AL_GetProductAtURL(const HTML& html, tb::thread_safe_memory_arena& arena);
CURLOptions AL_GetProductSearchCURLOptions(std::string_view query);

namespace stores
{

constexpr Store SuperValu =
{
    .id = StoreID::SUPERVALU, .name = "SuperValu", .prefix = "SV",
    .homepage = "https://shop.supervalu.ie/sm/delivery/rsid/5550",
    .root_url = "https://shop.supervalu.ie",
    .region = Region::IE,
    .ParseProductSearch = SV_ParseProductSearch,
    .GetProductSearchURL = SV_GetProductSearchURL,
    .GetProductAtURL = SV_GetProductAtURL,
    .GetProductSearchCURLOptions = Default_GetProductSearchCURLOptions
};

constexpr Store Tesco = {
    .id = StoreID::TESCO, .name = "Tesco", .prefix = "TE",
    .homepage = "https://www.tesco.ie/shop/en-IE",
    .root_url = "https://www.tesco.ie",
    .region = Region::IE,
    .ParseProductSearch = TE_ParseProductSearch,
    .GetProductSearchURL = TE_GetProductSearchURL,
    .GetProductAtURL = TE_GetProductAtURL,
    .GetProductSearchCURLOptions = Default_GetProductSearchCURLOptions
};

constexpr Store DunnesStores = {
    .id = StoreID::DUNNES_STORES, .name = "Dunnes Stores", .prefix = "DS",
    .homepage = "https://www.dunnesstoresgrocery.com",
    .root_url = "https://www.dunnesstoresgrocery.com",
    .region = Region::IE,
    .ParseProductSearch = DS_ParseProductSearch,
    .GetProductSearchURL = DS_GetProductSearchURL,
    .GetProductAtURL = DS_GetProductAtURL,
    .GetProductSearchCURLOptions = Default_GetProductSearchCURLOptions
};

constexpr Store Aldi = {
    .id = StoreID::ALDI, .name = "Aldi", .prefix = "AL",
    .homepage = "https://aldi.ie",
    .root_url = "https://aldi.ie",
    .region = Region::IE,
    .ParseProductSearch = AL_ParseProductSearch,
    .GetProductSearchURL = AL_GetProductSearchURL,
    .GetProductAtURL = AL_GetProductAtURL,
    .GetProductSearchCURLOptions = AL_GetProductSearchCURLOptions
};

}
