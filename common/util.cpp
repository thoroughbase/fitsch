#include "util.hpp"

#include "product.hpp"

#include <new>

void Abort_AllocFailed()
{
    Log(LogLevel::SEVERE, "Failed to allocate memory");
    throw std::bad_alloc {};
}

void to_json(json& j, const tb::arena_string& string)
{
	to_json(j, std::string_view { string.begin(), string.end() });
}

void from_json(const json& j, tb::arena_string& string)
{
	string = j.get<std::string_view>();
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

void to_json(json& j, const PricePU& p)
{
    j = json::array({ p.unit, p.price });
}

void from_json(const json& j, PricePU& p)
{
    p.unit = j[0];
    p.price = j[1];
}
