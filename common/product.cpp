#include "common/product.hpp"

#include <iostream>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <ranges>

// Constants

constexpr auto UNIT_SUFFIXES = std::to_array<std::string_view>({
    "", " each", "/kg", "/l", "/m²", "m"
});

constexpr auto PRICE_UNIT_SEPARATORS = std::to_array<std::string_view>({
    " per ", "/", " "
});

static_assert(tb::is_sorted(PRICE_UNIT_SEPARATORS, [] (auto& a, auto& b) {
    return a.size() >= b.size();
}), "Price unit separators array must be sorted from longest to shortest");

const std::unordered_map<Currency, std::string_view> CURRENCY_SYMBOLS = {
    { Currency::EUR, "€" }
};

const std::unordered_map<std::string_view, std::pair<Unit, float>>
  UNIT_CONVERSIONS = {
    { "kg",         { Unit::Kilogrammes, 1 } },
    { "kg drained", { Unit::Kilogrammes, 1 } },   // FIXME: Treated the same for now
    { "kne",        { Unit::Kilogrammes, 1 } },
    { "g",          { Unit::Kilogrammes, 1000 } },
    { "100g",       { Unit::Kilogrammes, 10 } },
    { "75cl",       { Unit::Litres, 1 / 0.75f } },
    { "cl",         { Unit::Litres, 1 / 0.75f } }, // ALDI 'CL' is always 75CL
    { "750ml",      { Unit::Litres, 1 / 0.75f } },
    { "70cl",       { Unit::Litres, 1 / 0.7f } },
    { "l",          { Unit::Litres, 1 } },
    { "litre",      { Unit::Litres, 1 } },
    { "ml",         { Unit::Litres, 1000 } },
    { "100ml",      { Unit::Litres, 10 } },
    { "m²",         { Unit::SqMetres, 1 } },
    { "each",       { Unit::Piece, 1 } },
    { "ea",         { Unit::Piece, 1 } },
    { "pac",        { Unit::Piece, 1 } },
    { "100sht",     { Unit::Piece, 0.01f } },
    { "100 sheets", { Unit::Piece, 0.01f } },
    { "sht",        { Unit::Piece, 0.01f } }, // ALDI 'sht' is always 100 sheets
    { "20 bag",     { Unit::Piece, 0.05f } },
    { "metre",      { Unit::Metres, 1 } },
    { "m",          { Unit::Metres, 1 } }
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

template<>
std::optional<tb::match_result<Price>> tb::try_match_single(std::string_view view)
{
    std::string str(view);

    size_t characters_consumed = 0;
    if (size_t comma = str.find(','); comma != std::string::npos) {
        str.erase(comma, 1);
        ++characters_consumed;
    }

    Price price;
    for (const auto& [currency, symbol] : CURRENCY_SYMBOLS) {
        if (size_t currency_pos = str.find(symbol); currency_pos != std::string::npos) {
            price.currency = currency;
            str.erase(currency_pos, symbol.size());
            characters_consumed += symbol.size();
            break;
        }
    }

    view = str;
    size_t ss_point = view.find('.');
    if (ss_point != std::string::npos) {
        auto parts = tb::try_match<unsigned, unsigned>(view, "{}.{}");
        if (!parts)
            return std::nullopt;

        auto [int_part, frac_part] = parts.value().object;
        price.value = (int_part * 100) + frac_part;
        characters_consumed += parts.value().characters_matched;
    } else {
        auto int_part = tb::try_match_single<unsigned>(view.substr(0, ss_point));
        if (!int_part)
            return std::nullopt;
        price.value = int_part.value().object * 100;
        characters_consumed += int_part.value().characters_matched;
    }

    return tb::match_result<Price> {
        .object = price,
        .characters_matched = characters_consumed
    };
}

std::optional<Price> Price::FromString(std::string_view str)
{
    auto price = tb::try_match_single<Price>(str);
    if (!price)
        return std::nullopt;

    return price.value().object;
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

std::optional<PricePU> PricePU::FromString(std::string_view str)
{
    if (str.empty()) return {};

    size_t separator_index;
    auto separator = std::ranges::find_if(PRICE_UNIT_SEPARATORS,
        [&separator_index, str] (std::string_view sep) {
            separator_index = str.find(sep);
            return separator_index != std::string::npos;
        }
    );

    if (separator == PRICE_UNIT_SEPARATORS.end()) {
        Log(LogLevel::WARNING, "Unrecognised delimiter/unit for '{}'!", str);
        return std::nullopt;
    }

    separator_index += separator->size() - 1;

    std::string_view unit_view = str;
    unit_view.remove_prefix(separator_index + 1);
    auto lowercase_unit = unit_view | std::views::transform(tolower)
                        | tb::range_to<std::string>();
    std::string_view price_view(str.data(), separator_index);

    if (!UNIT_CONVERSIONS.contains(lowercase_unit)) {
        Log(LogLevel::WARNING, "Unrecognised unit for '{}'!", str);
        Log(LogLevel::WARNING, "Offending unit: {}", unit_view);
        return std::nullopt;
    }

    auto [unit_type, factor] = UNIT_CONVERSIONS.at(lowercase_unit);
    std::optional<Price> price = Price::FromString(price_view);
    if (!price) return std::nullopt;

    return PricePU { price.value() * factor, unit_type };
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

ProductList::ProductList(size_t depth) : depth(depth) {}

void ProductList::Add(const ProductList& other)
{
    products.insert(products.end(), other.products.begin(), other.products.end());
    if (other.depth < depth) depth = other.depth;
}

QueryTemplate ProductList::AsQueryTemplate(std::string_view querystr,
                                           StoreSelection ids) const
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
