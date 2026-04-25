#pragma once

#include <string>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <algorithm>
#include <span>
#include <nlohmann/json.hpp>

#include <fmt/format.h>
#include <fmt/chrono.h>

#include <tb/tb.h>

struct Price;
struct PricePU;

enum class LogLevel { DEBUG = 0, INFO = 1, WARNING = 2, SEVERE = 3 };
constexpr auto MIN_LOG_LEVEL = LogLevel::DEBUG;

template<typename... T>
void Log(LogLevel l, fmt::format_string<T...> format, T&&... args)
{
    constexpr static auto LEVEL_NAMES = std::to_array<std::string_view>({
        "DEBUG", "INFO", "WARNING", "SEVERE"
    });

    if (l < MIN_LOG_LEVEL) return;

    fmt::print("[{:%Y/%m/%d %H:%M:%S} {}] {}\n",
               fmt::localtime(std::time(nullptr)),
               LEVEL_NAMES[static_cast<size_t>(l)],
               fmt::vformat(format, fmt::make_format_args(args...)));
}

void Abort_AllocFailed();

namespace util
{

template<std::ranges::range Container, typename Callable>
constexpr bool is_sorted(const Container& container, Callable&& predicate)
{
    for (auto iter = container.cbegin(); iter != container.cend(); ++iter) {
        if (iter == container.cbegin()) continue;
        if (!predicate(*(iter - 1), *iter)) return false;
    }

    return true;
}

}

using nlohmann::json;

template<typename E>
void to_json(json& j, tb::enum_selection<E> es)
{
    j = es._enum_field;
}

template<typename E>
void from_json(const json& j, tb::enum_selection<E>& es)
{
    es = j.get<typename tb::enum_selection<E>::IntegerType>();
}

template<typename... Ts>
void to_json(json& j, const std::variant<Ts...>& variant)
{
    std::visit([&] (const auto& variant) { to_json(j, variant); }, variant);
}

template<typename... Ts>
void from_json(const json& j, std::variant<Ts...>& variant)
{
    std::visit([&] (auto& variant) { from_json(j, variant); }, variant);
}

void to_json(json& j, const tb::arena_string& string);
void from_json(const json& j, tb::arena_string& string);

// Price struct is serialised as a tuple of numerical values
void to_json(json& j, const Price& p);
void from_json(const json& j, Price& p);

// Price per unit struct is serialised as a tuple of unit & price tuple
void to_json(json& j, const PricePU& p);
void from_json(const json& j, PricePU& p);

template<typename T>
void to_json(json& j, const tb::arena_vector<T>& vec)
{
	to_json(j, std::ranges::ref_view { vec });
}

template<typename T>
void from_json(const json& j, tb::arena_vector<T>& vec)
{
	std::ranges::copy(j | std::views::transform([] (const json& j) -> T {
		return j.get<T>();
	}), vec.begin());
}

template<typename K, typename V>
void to_json(json& j, const tb::arena_unordered_map<K, V>& map)
{
    j = json::object();
	for (const auto& [key, value] : map)
		j.emplace(key, value);
}

template<typename K, typename V>
void from_json(const json& j, tb::arena_unordered_map<K, V>& map)
{
	map.clear();
	for (const auto& [key, value] : j.items())
		map.emplace(key, value);
}
