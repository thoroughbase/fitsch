#include "common/product.hpp"

#include <iostream>
#include <cstdlib>
#include <ctime>
#include <ranges>

// Constants

constexpr std::string_view UNIT_SUFFIXES[] = {
	"", " each", "/kg", "/l", "/m²", "m"
};

const std::unordered_map<Currency, std::string_view> CURRENCY_SYMBOLS = {
    { Currency::EUR, "€" }
};

const std::unordered_map<std::string_view, std::pair<Unit, float>>
  UNIT_CONVERSIONS = {
    { "kg",     { Unit::Kilogrammes, 1 } },
    { "g",      { Unit::Kilogrammes, 1000 } },
    { "75cl",   { Unit::Litres, 1 / 0.75f } },
    { "70cl",   { Unit::Litres, 1 / 0.7f } },
    { "l",      { Unit::Litres, 1 } },
    { "litre",  { Unit::Litres, 1 } },
    { "ml",     { Unit::Litres, 1000 } },
    { "m²",     { Unit::SqMetres, 1 } },
    { "each",   { Unit::Piece, 1 } },
    { "100sht", { Unit::Piece, 0.01f } },
    { "metre",  { Unit::Metres, 1 } }
};

// Price

std::string Price::ToString() const
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

Price Price::FromString(std::string str)
{
    Price price;
    size_t comma;
    if ((comma = str.find(',')) != std::string::npos)
        str.erase(comma, 1);

    std::string_view view(str);

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
        if (ss_point != std::string::npos)
            price.value += std::stoi(view.substr(ss_point + 1).data());
    } catch (const std::exception& e) {
        Log(LogLevel::WARNING, "Error converting string {} to Price: {}", str, e.what());
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

std::string PricePU::ToString() const
{
	const std::string_view& suffix = UNIT_SUFFIXES[static_cast<size_t>(unit)];
    return price.ToString().append(suffix.begin(), suffix.end());
}

PricePU PricePU::FromString(std::string_view str)
{
    if (str.empty()) return {};

    size_t delimiter;
    if ((delimiter = str.find('/')) == std::string::npos) {
        if ((delimiter = str.find(' ')) == std::string::npos) {
            Log(LogLevel::WARNING, "Unrecognised delimiter/unit for '{}'!", str);
            return {};
        }
    }

    std::string_view unit_view = str;
    unit_view.remove_prefix(delimiter + 1);
    std::string_view price_view(str.data(), delimiter);

    if (!UNIT_CONVERSIONS.contains(unit_view)) {
        Log(LogLevel::WARNING, "Unrecognised unit for '{}'!", str);
        return {};
    }

    auto [unit_type, factor] = UNIT_CONVERSIONS.at(unit_view);
    return { Price::FromString(std::string { price_view }) * factor, unit_type };
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

// ProductList

ProductList::ProductList(int d) : depth(d) {}

void ProductList::Add(const ProductList& other)
{
    products.insert(products.end(), other.products.begin(), other.products.end());
    if (depth == SEARCH_DEPTH_INDEFINITE && other.depth != depth) {
        depth = other.depth;
        return;
    }
    if (other.depth < depth && other.depth != SEARCH_DEPTH_INDEFINITE)
        depth = other.depth;
}

QueryTemplate ProductList::AsQueryTemplate(std::string_view querystr,
                                           const StoreSelection& ids) const
{
    QueryTemplate q_template {
        .query_string { querystr }, .stores { ids },
        .timestamp = std::time(nullptr), .depth = depth
    };

    q_template.results.reserve(products.size());

    for (const auto& [product, relevance] : products)
        q_template.results.emplace(product.id, relevance);

    return q_template;
}

std::vector<Product> ProductList::AsProductVector() const
{
    return std::views::keys(products) | tb::range_to<std::vector<Product>>();
}
