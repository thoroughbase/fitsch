#pragma once

#include <string>
#include <string_view>
#include <compare>
#include <vector>
#include <unordered_map>
#include <ctime>

#include <nlohmann/json.hpp>

#include "common/util.hpp"

#include <tb/tb.h>

using nlohmann::json;

enum class StoreID
{
    SUPERVALU = 1 << 0,
    LIDL = 1 << 1,
    TESCO = 1 << 2,
    ALDI = 1 << 3,
    DUNNES_STORES = 1 << 4
};

enum class Region { IE };

enum class Unit
{
    None = 0, Piece = 1, Kilogrammes = 2, Litres = 3,
    SqMetres = 4, Metres = 5
};

enum class Currency { EUR };

struct Price
{
    std::string ToString() const;
    static std::optional<Price> FromString(std::string_view str);

    std::partial_ordering operator<=>(const Price& other) const;
    Price operator*(float b) const;

    Currency currency = Currency::EUR;
    unsigned value = 0;
};

struct PricePU
{
    std::string ToString() const;
    static std::optional<PricePU> FromString(std::string_view str);

    std::partial_ordering operator<=>(const PricePU& other) const;

    Price price { Currency::EUR, 0 };
    Unit unit = Unit::None;
};

using StoreSelection = tb::enum_selection<StoreID>;

enum class OfferType
{
    MULTIPLE_FOR_REDUCED_PRICE, // "x for €Y"
    MULTIPLE_HETEROGENEOUS_FOR_REDUCED_PRICE, // "Any x for €Y of ..."
    REDUCED_PRICE_ABSOLUTE, // "Only €Y"
    REDUCED_PRICE_PERCENTAGE,
    REDUCED_PRICE_DEDUCTION,
    GREAT_VALUE,
    BUNDLE_OFFER,
    MEMBERSHIP_DEAL_ONLY
};

template<typename AATypes>
struct BasicOffer
{
    AATypes::string text;
    Price price;
    size_t bulk_amount = 0;
    std::time_t expiry;
    OfferType type;
    bool membership_only = false;
    float price_reduction_multiplier = 1;

    static auto FromString(std::string_view text,
        tb::thread_safe_memory_arena* arena = nullptr) -> std::optional<BasicOffer>;
    auto ToString() const -> std::string;

    template<typename T = AATypes> requires
        (std::same_as<T, AATypes> && T::arena_type_set)
    static auto WithArena(tb::thread_safe_memory_arena& arena) -> BasicOffer
    {
        return {
            .text { arena }
        };
    }
};

using Offer = BasicOffer<tb::default_aa_types>;
using ArenaOffer = BasicOffer<tb::arena_aa_types>;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Offer, text, price, bulk_amount, expiry, type,
    membership_only, price_reduction_multiplier);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ArenaOffer, text, price, bulk_amount, expiry, type,
    membership_only, price_reduction_multiplier);

template<>
auto Offer::FromString(std::string_view, tb::thread_safe_memory_arena*)
    -> std::optional<Offer>;

template<>
auto ArenaOffer::FromString(std::string_view, tb::thread_safe_memory_arena*)
    -> std::optional<ArenaOffer>;

template<>
auto Offer::ToString() const -> std::string;

template<>
auto ArenaOffer::ToString() const -> std::string;

template<typename AATypes>
struct BasicProduct
{
    AATypes::string name, description, image_url, url, id;
    AATypes::template vector<BasicOffer<AATypes>> offers;
    Price item_price;
    PricePU price_per_unit; // Price per KG, L, etc.
    StoreID store;
    std::time_t timestamp;

    bool full_info;

    template<typename T = AATypes> requires
        (std::same_as<T, AATypes> && T::arena_type_set)
    static auto WithArena(tb::thread_safe_memory_arena& arena) -> BasicProduct
    {
        return {
            .name { arena },
            .description { arena },
            .image_url { arena },
            .url { arena },
            .id { arena },
            .offers { arena }
        };
    }
};

using Product = BasicProduct<tb::default_aa_types>;
using ArenaProduct = BasicProduct<tb::arena_aa_types>;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Product, name, offers, description, image_url, url,
    id, item_price, price_per_unit, store, timestamp, full_info);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ArenaProduct, name, offers, description, image_url,
    url, id, item_price, price_per_unit, store, timestamp, full_info);

struct QueryResultInfo
{
    size_t relevance; // What place an item appears at for a particular search term
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(QueryResultInfo, relevance);

// Database representation of queries - Product IDs + extra query info
template<typename AATypes>
struct BasicQueryTemplate
{
    AATypes::string query_string;
    StoreSelection stores;
    AATypes::template unordered_map<typename AATypes::string, QueryResultInfo> results;
    std::time_t timestamp;
    size_t depth;

    template<typename T = AATypes> requires
        (std::same_as<T, AATypes> && T::arena_type_set)
    static auto WithArena(tb::thread_safe_memory_arena& arena) -> BasicQueryTemplate
    {
        return {
            .query_string { arena },
            .results { arena }
        };
    }
};

using QueryTemplate = BasicQueryTemplate<tb::default_aa_types>;
using ArenaQueryTemplate = BasicQueryTemplate<tb::arena_aa_types>;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(QueryTemplate, query_string,
    stores, results, timestamp, depth);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ArenaQueryTemplate, query_string,
    stores, results, timestamp, depth);

constexpr size_t SEARCH_DEPTH_INDEFINITE = std::numeric_limits<size_t>::max();

using PMRProduct = std::variant<Product, ArenaProduct>;
using SearchResult = std::pair<PMRProduct&, QueryResultInfo>;

template<typename AATypes>
struct BasicProductList
{
    AATypes::template vector<SearchResult> products;
    size_t depth = SEARCH_DEPTH_INDEFINITE;

    template<typename T = AATypes> requires
        (std::same_as<T, AATypes> && T::arena_type_set)
    static auto WithArena(tb::thread_safe_memory_arena& arena) -> BasicProductList
    {
        return {
            .products { arena }
        };
    }
};

using ProductList = BasicProductList<tb::default_aa_types>;
using ArenaProductList = BasicProductList<tb::arena_aa_types>;
