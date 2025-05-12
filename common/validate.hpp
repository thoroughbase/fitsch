#pragma once

#include <buxtehude/validate.hpp>

#include <nlohmann/json.hpp>

namespace validate
{

using nlohmann::json;

constexpr auto IsStringArray = [] (const json& j) -> bool {
    if (!j.is_array()) return false;
    for (auto& element : j)
        if (!element.is_string()) return false;
    return true;
};

inline const buxtehude::ValidationSeries QUERY_RESULT = {
    { "/term"_json_pointer, buxtehude::predicates::NotEmpty },
    { "/items"_json_pointer, [] (const json& j) { return j.is_array(); } },
    { "/request-id"_json_pointer, [] (const json& j) { return j.is_number(); } }
};

inline const buxtehude::ValidationSeries QUERY = {
    { "/terms"_json_pointer, IsStringArray },
    { "/request-id"_json_pointer, [] (const json& j) { return j.is_number(); } },
    { "/stores"_json_pointer, [] (const json& j) { return j.is_number(); } },
    { "/depth"_json_pointer, [] (const json& j) { return j.is_number(); } }
};

}
