#include "common/product.hpp"

#include <iostream>
#include <cstdlib>
#include <ctime>

// Constant hash-maps

const std::unordered_map<Currency, std::string_view> CURRENCY_SYMBOLS = {
    { EUR, "€" }
};

const std::unordered_map<std::string_view, std::pair<Unit, float>>
  UNIT_CONVERSIONS = {
    { "kg",   { Unit::Kilogrammes, 1 } },
    { "75cl", { Unit::Litres, 1 / 0.75f } },
    { "70cl", { Unit::Litres, 1 / 0.7f } },
    { "l",    { Unit::Litres, 1 } },
    { "ml",   { Unit::Litres, 1000 } },
    { "m²",   { Unit::SqMetres, 1 } },
    { "each", { Unit::None, 1 } }
};

// Price

string Price::ToString() const
{
    std::string result = std::to_string(value);

    result.insert(0, CURRENCY_SYMBOLS.at(currency).data());
    if (value >= 100)
        result.insert(result.size() - 2, ".");
    else if (value >= 10)
        result.insert(result.size() - 2, "0.");
    else
        result.insert(result.size() - 1, "0.0");

    return result;
}

Price Price::FromString(string str)
{
	Price price;
    size_t comma;
    if ((comma = str.find(',')) != string::npos)
        str.erase(comma, 1);

    std::string_view view(str);

    size_t ss_start;

    for (auto& [cur, symbol] : CURRENCY_SYMBOLS) {
        if (view.find(symbol) != 0) continue;
        else { // Assume symbol can only occur before numerical values
            price.currency = cur;
            view.remove_prefix(symbol.size());
            break;
        }
    }

    size_t ss_point = view.find('.');

    try {
        price.value = std::stoi(view.substr(0, ss_point).data()) * 100;
        if (ss_point != string::npos)
            price.value += std::stoi(view.substr(ss_point + 1).data());
    } catch (const std::exception& e) {
        Log(WARNING, "Error converting string {} to Price: {}", str, e.what());
        return {};
    }

    return price;
}

std::partial_ordering Price::operator<=>(const Price& other) const
{
    if (currency != other.currency) return std::partial_ordering::unordered;
    return value <=> other.value;
}

Price Price::operator*(float f) const
{
    return { currency, static_cast<unsigned>(value * f) };
}

void to_json(json& j, const Price& p)
{
    j = { json::array({ p.currency, p.value }) };
}

void from_json(const json& j, Price& p)
{
    p.currency = j[0];
    p.value = j[1];
}

// Price per unit

string PricePU::ToString() const
{
    return price.ToString() + UNIT_SUFFIXES[unit];
}

PricePU PricePU::FromString(const string& str)
{
    if (str.empty()) return {};

    size_t delimiter;
    if ((delimiter = str.find('/')) == string::npos) {
        if ((delimiter = str.find(' ')) == string::npos) {
            Log(WARNING, "Unrecognised delimiter/unit for '{}'!", str);
            return {};
        }
    }

    std::string_view unit_view(str.data() + delimiter + 1);
    std::string_view price_view(str.data(), delimiter);

    if (!UNIT_CONVERSIONS.contains(unit_view)) {
        Log(WARNING, "Unrecognised unit for '{}'!", str);
        return {};
    }

    auto [unit_type, factor] = UNIT_CONVERSIONS.at(unit_view);
    return { Price::FromString(std::string(price_view)) * factor, unit_type };
}

std::partial_ordering PricePU::operator<=>(const PricePU& other) const
{
	if (unit != other.unit) return std::partial_ordering::unordered;
	return price <=> other.price;
}

void to_json(json& j, const PricePU& p)
{
    j = json::array({ p.unit, p.price });
}

void from_json(const json& j, PricePU& p)
{
    p.unit = j[0];
    p.price = j[1];
}

// StoreSelection

StoreSelection::StoreSelection(StoreID id) { push_back(id); }

bool StoreSelection::Has(StoreID id) const
{
    for (auto it = begin(); it < end(); ++it)
        if (*it == id) return true;

    return false;
}

bool StoreSelection::Has(const StoreSelection& selection) const
{
    if (selection.size() > size()) return false;

    for (StoreID i : selection)
        if (!Has(i)) return false;

    return true;
}

void StoreSelection::Remove(StoreID id)
{
    for (auto it = begin(); it < end();)
        if (*it == id) it = erase(it);
        else ++it;
}

void StoreSelection::Remove(const StoreSelection& s)
{
    for (StoreID i : s) Remove(i);
}

void StoreSelection::Add(StoreID id)
{
    if (!Has(id)) push_back(id);
}

// ProductList

ProductList::ProductList(int d) : depth(d) {}

ProductList::ProductList(const Product& p)
{
    emplace_back(p, QueryResultInfo {0});
}

ProductList::ProductList(const std::vector<Product>& products, int d) : depth(d)
{
	reserve(products.size());

    int i = 0;
    for (auto& p : products)
        emplace_back(p, QueryResultInfo { i++ });
}

void ProductList::Add(const ProductList& l)
{
    insert(end(), l.begin(), l.end());
    if (l.depth != 0 && l.depth < depth)
        depth = l.depth;
}

Product ProductList::First() const
{
    if (size()) return at(0).first;

    return PRODUCT_ERROR;
}

QueryTemplate ProductList::AsQueryTemplate(const string& querystr,
                                           const StoreSelection& ids) const
{
    QueryTemplate tmpl { .query_string = querystr, .stores = ids,
                         .timestamp = std::time(nullptr), .depth = depth };

    for (auto it = begin(); it != end(); ++it)
        tmpl.results.emplace((*it).first.id, (*it).second);

    return tmpl;
}

std::vector<Product> ProductList::AsProductVector() const
{
    std::vector<Product> r;
    r.reserve(size());

    for (auto it = begin(); it != end(); ++it)
        r.push_back((*it).first);

    return r;
}
