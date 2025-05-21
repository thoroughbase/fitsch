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

template<typename T>
struct disable_enum_selection;

namespace detail
{

template<typename T>
struct dependent_false : std::false_type {};

template<typename T>
concept enum_selection_disabled = requires { { disable_enum_selection<T> {} }; };

struct bad_tuple_access {};

template<size_t I, typename Tuple, typename Callable>
constexpr void visit_tuple_impl(Tuple& tuple, size_t index, Callable&& fn)
{
    if constexpr (I == 0) {
        throw bad_tuple_access {};
    } else {
        if (index == I - 1)
            fn(std::get<I - 1>(tuple));
        else
            visit_tuple_impl<I - 1>(tuple, index, fn);
    }
}

inline void constexpr_static_error(std::string_view) {}

constexpr std::string_view MATCH_PLACEHOLDER = "{}";

static constexpr size_t count_placeholders(std::string_view view)
{
    size_t placeholder_count = 0;
    size_t pos = 0;
    while ((pos = view.find(MATCH_PLACEHOLDER)) != std::string::npos) {
        ++placeholder_count;
        view.remove_prefix(pos + MATCH_PLACEHOLDER.size());
    }
    return placeholder_count;
}

template<typename... Ts>
struct match_string
{
    using identity = match_string<Ts...>;

    consteval match_string(auto view) : view(view)
    {
        if (count_placeholders(view) != sizeof...(Ts))
            constexpr_static_error("Incorrect number of placeholders");
    }

    const std::string_view view;
};

}

template<typename EnumType> requires std::is_enum_v<EnumType>
struct enum_selection
{
    using IntegerType = std::make_unsigned_t<std::underlying_type_t<EnumType>>;

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

template<typename E>
void to_json(json& j, enum_selection<E> es)
{
    j = es._enum_field;
}

template<typename E>
void from_json(const json& j, enum_selection<E>& es)
{
    es = j.get<typename enum_selection<E>::IntegerType>();
}

template<typename Tuple, typename Callable>
constexpr void visit_tuple(Tuple& tuple, size_t index, Callable&& fn)
{
    detail::visit_tuple_impl<std::tuple_size_v<Tuple>>(tuple, index, fn);
}

template<typename T>
struct match_result
{
    T object;
    size_t characters_matched;
};

template<typename T>
std::optional<match_result<T>> try_match_single(std::string_view);

template<typename T> requires std::unsigned_integral<T>
constexpr std::optional<match_result<T>> try_match_single(std::string_view s)
{
    const char* end = s.data();
    match_result<T> result {
        .object = static_cast<T>(
            // endptr of strtoull for some reason is not pointer-to-const char
            std::strtoull(s.data(), const_cast<char**>(&end), 10)
        ),
        .characters_matched = static_cast<size_t>(end - s.data())
    };
    if (errno == ERANGE || result.object > std::numeric_limits<T>::max()
        || result.characters_matched < 1)
        return std::nullopt;

    return result;
}

template<typename... Ts>
constexpr auto try_match(std::string_view string,
    typename detail::match_string<Ts...>::identity _match)
-> std::optional<match_result<std::tuple<Ts...>>>
{
    size_t current_placeholder = 0;
    std::tuple<Ts...> result;

    const size_t start_size = string.size();
    std::string_view match = _match.view;

    while (!match.empty()) {
        const size_t placeholder_index = match.find(detail::MATCH_PLACEHOLDER);
        if (match.substr(0, placeholder_index) != string.substr(0, placeholder_index))
            return std::nullopt;

        match.remove_prefix(placeholder_index);
        string.remove_prefix(placeholder_index);

        if (match.empty()) break;

        bool okay = false;
        visit_tuple(result, current_placeholder, [&] (auto& elem) {
            using ElementType = std::remove_reference_t<decltype(elem)>;
            std::optional<match_result<ElementType>> single
                = try_match_single<ElementType>(string);
            if (single) {
                elem = std::move(single.value().object);
                okay = true;
                match.remove_prefix(detail::MATCH_PLACEHOLDER.size());
                string.remove_prefix(single.value().characters_matched);
            }
        });

        if (!okay)
            return std::nullopt;

        ++current_placeholder;
    }

    return match_result<decltype(result)> {
        .object = result,
        .characters_matched = start_size - string.size()
    };
}

}

template<typename E>
    requires std::is_enum_v<E> && (!tb::detail::enum_selection_disabled<E>)
constexpr tb::enum_selection<E> operator|(E a, E b)
{
    return tb::enum_selection<E>::to_int(a) | tb::enum_selection<E>::to_int(b);
}
