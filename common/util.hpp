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
