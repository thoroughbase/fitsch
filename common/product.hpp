#pragma once

#include <string>
#include <string_view>
#include <compare>
#include <vector>
#include <unordered_map>
#include <ctime>

#include <nlohmann/json.hpp>

#include "common/util.hpp"

using nlohmann::json;

enum class StoreID { SUPERVALU, LIDL, TESCO, ALDI, DUNNES_STORES };
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
    static Price FromString(std::string str);

    std::partial_ordering operator<=>(const Price& other) const;
    Price operator*(float b) const;

    Currency currency = Currency::EUR;
    unsigned value = 0;
};

// Price struct is serialised as a tuple of numerical values
void to_json(json& j, const Price& p);
void from_json(const json& j, Price& p);

struct PricePU
{
    std::string ToString() const;
    static PricePU FromString(std::string_view str);

    std::partial_ordering operator<=>(const PricePU& other) const;

    Price price { Currency::EUR, 0 };
    Unit unit = Unit::None;
};

// Price per unit struct is serialised as a tuple of unit & price tuple
void to_json(json& j, const PricePU& p);
void from_json(const json& j, PricePU& p);

using StoreSelection = std::vector<StoreID>;

struct Product
{
    std::string name, description, image_url, url, id;
    Price item_price;
    PricePU price_per_unit; // Price per KG, L, etc.
    StoreID store;
    std::time_t timestamp;

    bool full_info;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Product, name, description, image_url, url, id,
    item_price, price_per_unit, store, timestamp, full_info);

struct QueryResultInfo
{
    int relevance; // What place an item appears at for a particular search term
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(QueryResultInfo, relevance);

// Database representation of queries - Product IDs + extra query info
struct QueryTemplate
{
    std::string query_string;
    StoreSelection stores;
    std::unordered_map<std::string, QueryResultInfo> results;
    std::time_t timestamp;
    int depth;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(QueryTemplate, query_string, stores, results,
    timestamp, depth);

constexpr int SEARCH_DEPTH_INDEFINITE = -1;

struct ProductList
{
    ProductList(int depth = SEARCH_DEPTH_INDEFINITE);

    void Add(const ProductList& other);

    QueryTemplate AsQueryTemplate(std::string_view querystr,
                                  const StoreSelection& ids) const;
    std::vector<Product> AsProductVector() const;

    std::vector<std::pair<Product, QueryResultInfo>> products;
    int depth;
};
