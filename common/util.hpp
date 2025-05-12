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

enum class LogLevel { DEBUG = 0, INFO = 1, WARNING = 2, SEVERE = 3 };
constexpr auto MIN_LOG_LEVEL = LogLevel::DEBUG;

template <typename... T>
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

template<std::ranges::range Container, typename Callable>
constexpr bool is_sorted(const Container& container, Callable&& predicate)
{
    for (auto iter = container.cbegin(); iter != container.cend(); ++iter) {
        if (iter == container.cbegin()) continue;
        if (!predicate(*(iter - 1), *iter)) return false;
    }

    return true;
}

template<typename Lambda>
struct scoped_guard
{
    Lambda lambda;
    scoped_guard(Lambda&& lambda) : lambda(lambda) {}
    ~scoped_guard() { lambda(); }
};

template<class T>
struct disable_enum_selection;

namespace detail
{

template<class T>
struct get_enum_int;

template<class T, class U>
concept same_size = sizeof(T) == sizeof(U);

template<class T> requires same_size<T, uint8_t>
struct get_enum_int<T> { using type = uint8_t; };

template<class T> requires same_size<T, uint16_t>
struct get_enum_int<T> { using type = uint16_t; };

template<class T> requires same_size<T, uint32_t>
struct get_enum_int<T> { using type = uint32_t; };

template<class T> requires same_size<T, uint64_t>
struct get_enum_int<T> { using type = uint64_t; };

template<class T>
struct dependent_false : std::false_type {};

template<class T>
concept enum_selection_disabled = requires { { disable_enum_selection<T> {} }; };

}

template<class EnumType> requires std::is_enum_v<EnumType>
struct enum_selection
{
    using IntegerType = typename detail::get_enum_int<EnumType>::type;

    struct iterator
    {
        constexpr iterator(IntegerType u) : _underlying(u)
        {
            while (_underlying != 0 && (_underlying & 1) == 0) {
                _underlying >>= 1;
                ++_bit;
            }
        }

        constexpr EnumType operator*() { return static_cast<EnumType>(1 << _bit); }

        constexpr bool operator!=(const iterator& other)
        {
            return other._underlying != _underlying;
        }

        constexpr void operator++()
        {
            do {
                _underlying >>= 1;
                ++_bit;
            } while (_underlying != 0 && (_underlying & 1) == 0);
        }

        IntegerType _underlying = 0;
        int _bit = 0;
    };

    constexpr enum_selection() : _enum_field(0) {}

    constexpr static IntegerType to_int(auto e)
    {
        if constexpr (std::is_same_v<decltype(e), IntegerType>)
            return e;
        else if constexpr (std::is_same_v<decltype(e), EnumType>)
            return static_cast<IntegerType>(e);
        else if constexpr (std::is_same_v<decltype(e), int>) // integer promotion
            return static_cast<IntegerType>(e);
        else if constexpr (std::is_same_v<decltype(e), enum_selection<EnumType>>)
            return e._enum_field;
        else {
            static_assert(detail::dependent_false<decltype(e)> {},
                "Incompatible type for enum_selection");
            return -1;
        }
    }

    constexpr enum_selection(auto e) : _enum_field(to_int(e)) {}

    constexpr enum_selection& operator|=(auto e)
    {
        _enum_field |= to_int(e);
        return *this;
    }

    constexpr enum_selection operator&(auto e) const { return _enum_field & to_int(e); }

    constexpr enum_selection operator|(auto e) const { return _enum_field | to_int(e); }

    constexpr enum_selection operator^(auto e) const { return _enum_field ^ to_int(e); }

    constexpr bool has(auto e) const { return (_enum_field & to_int(e)) == to_int(e); }

    constexpr enum_selection with_toggled(auto e) const
    {
        return _enum_field ^ to_int(e);
    }

    constexpr enum_selection without(auto e) const
    {
        return (_enum_field | to_int(e)) ^ to_int(e);
    }

    constexpr enum_selection& add(auto e)
    {
        _enum_field |= to_int(e);
        return *this;
    }

    constexpr enum_selection& toggle(auto e)
    {
        _enum_field ^= to_int(e);
        return *this;
    }

    constexpr bool operator==(auto e) { return _enum_field == to_int(e); }

    constexpr bool operator!=(auto e) { return _enum_field != to_int(e); }

    constexpr operator bool() const { return _enum_field; }

    constexpr IntegerType const as_int() { return _enum_field; }

    constexpr iterator begin() const { return iterator { _enum_field }; }

    constexpr iterator end() const { return iterator { 0 }; }

    IntegerType _enum_field;
};

using nlohmann::json;

template<class E>
void to_json(json& j, enum_selection<E> es)
{
    j = es._enum_field;
}

template<class E>
void from_json(const json& j, enum_selection<E> es)
{
    es = j.get<typename enum_selection<E>::IntegerType>();
}

}

template<class E> requires std::is_enum_v<E> && (!tb::detail::enum_selection_disabled<E>)
constexpr tb::enum_selection<E> operator|(E a, E b)
{
    return tb::enum_selection<E>::to_int(a) | tb::enum_selection<E>::to_int(b);
}
