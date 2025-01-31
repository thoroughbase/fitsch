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
using std::string;

enum StoreID { SUPERVALU, LIDL, TESCO, ALDI };
enum Region { IE };

enum Unit { None = 0, Piece = 1, Kilogrammes = 2, Litres = 3, SqMetres = 4, Metres = 5 };
inline const char* UNIT_SUFFIXES[] = { "", " each", "/kg", "/l", "/m²", "m" };

enum Currency { EUR };

struct Price
{
    string ToString() const;
	static Price FromString(string str);

	std::partial_ordering operator<=>(const Price& other) const;
    Price operator*(float b) const;

    Currency currency = EUR;
    unsigned value = 0;
};

// Price struct is serialised as a tuple of numerical values
void to_json(json& j, const Price& p);
void from_json(const json& j, Price& p);

struct PricePU
{
    string ToString() const;
    static PricePU FromString(std::string_view str);

    std::partial_ordering operator<=>(const PricePU& other) const;

    Price price { EUR, 0 };
    Unit unit = None;
};

// Price per unit struct is serialised as a tuple of unit & price tuple
void to_json(json& j, const PricePU& p);
void from_json(const json& j, PricePU& p);

using StoreSelection = std::vector<StoreID>;

struct Product
{
    string name, description, image_url, url, id;
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
    string query_string;
    StoreSelection stores;
    std::unordered_map<string, QueryResultInfo> results;
    std::time_t timestamp;
    int depth;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(QueryTemplate, query_string, stores, results,
    timestamp, depth);

inline const int SEARCH_DEPTH_INDEFINITE = -1;

struct ProductList
{
	ProductList(int depth = SEARCH_DEPTH_INDEFINITE);

	void Add(const ProductList& other);

    QueryTemplate AsQueryTemplate(const string& querystr, const StoreSelection& ids)
        const;
    std::vector<Product> AsProductVector() const;

	std::vector<std::pair<Product, QueryResultInfo>> products;
	int depth;
};
