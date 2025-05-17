#pragma once

#include <buxtehude/validate.hpp>

#include <nlohmann/json.hpp>

namespace validate
{

using nlohmann::json;
namespace bux = buxtehude;

constexpr auto IsStringArray = [] (const json& j) -> bool {
    if (!j.is_array()) return false;
    for (auto& element : j)
        if (!element.is_string()) return false;
    return true;
};

inline const buxtehude::ValidationSeries QUERY_RESULT = {
    { "/term"_json_pointer, bux::predicates::NotEmpty },
    { "/items"_json_pointer, bux::predicates::IsArray },
    { "/request-id"_json_pointer, bux::predicates::IsNumber }
};

inline const buxtehude::ValidationSeries QUERY = {
    { "/terms"_json_pointer, IsStringArray },
    { "/request-id"_json_pointer, bux::predicates::IsNumber },
    { "/stores"_json_pointer, bux::predicates::IsNumber },
    { "/depth"_json_pointer, bux::predicates::IsNumber },
    { "/force-refresh"_json_pointer, buxtehude::predicates::IsBool }
};

}
