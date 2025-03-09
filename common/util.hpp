#pragma once

#include <string>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <span>

#include <fmt/format.h>
#include <fmt/chrono.h>

#define MIN_LOG_LEVEL LogLevel::DEBUG

enum class LogLevel { DEBUG = 0, INFO = 1, WARNING = 2, SEVERE = 3 };

using std::string;

template <typename... T>
void Log(LogLevel l, fmt::format_string<T...> format, T&&... args)
{
    constexpr std::string_view LEVEL_NAMES[] = { "DEBUG", "INFO", "WARNING", "SEVERE" };

    if (l < MIN_LOG_LEVEL) return;

    fmt::print("[{} {:%H:%M:%S}] {}\n", LEVEL_NAMES[static_cast<size_t>(l)],
               fmt::localtime(std::time(nullptr)),
               fmt::vformat(format, fmt::make_format_args(args...)));
}

// Making up for lack of C++23/26 span & range features
namespace tb
{

template<typename T, size_t N>
constexpr auto make_span(T (&&array)[N]) { return std::span<T>(array, N); }

template<typename T>
struct range_to_dummy {};

template<typename T>
constexpr auto range_to() { return range_to_dummy<T> {}; }

template<std::ranges::view View, typename T>
constexpr auto operator|(View&& view, range_to_dummy<T>&& container)
{
    return T { view.begin(), view.end() };
}

}
