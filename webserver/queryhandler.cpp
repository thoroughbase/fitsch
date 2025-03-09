#include "queryhandler.hpp"

#include <buxtehude/validate.hpp>

#include <nlohmann/json.hpp>

#include "common/validate.hpp"

using nlohmann::json;

QueryHandler::QueryHandler(buxtehude::Client& bclient, std::string_view webscraper)
    : bclient(bclient), webscraper_name(webscraper)
{
    bclient.AddHandler("query-result", [this] (buxtehude::Client& cl,
        const buxtehude::Message& msg) {
        if (!buxtehude::ValidateJSON(msg.content, validate::QUERY_RESULT)) {
            Log(LogLevel::WARNING, "Invalid query-result message received!");
            return;
        }
        int id = msg.content["request-id"];
        std::string term = msg.content["term"];

        auto iterator = pending_queries.find(id);
        if (iterator == pending_queries.end()) return;

        auto& request_info = iterator->second;
        request_info.results[term].reserve(msg.content["items"].size());

        for (const json& j : msg.content["items"]) {
            request_info.results[term].emplace_back(j.get<Product>());
        }

        if (request_info.results.size() >= request_info.expecting) {
            request_info.promise.set_value(std::move(request_info.results));
            pending_queries.erase(iterator);
        }
    });
}

std::future<QueryResultsMap> QueryHandler::SendQuery(std::string_view query)
{
    auto [iterator, success] = pending_queries.emplace(request_id++, RequestInfo {
        .expecting = 1
    });

    auto& [id, request_info] = *iterator;

    request_info.results.emplace(query, std::vector<Product> {});

    bclient.Write({
        .dest = webscraper_name, .type = "query", .only_first = true,
        .content = {
            { "terms", json::array({ query }) },
            { "request-id", id }
        }
    });

    return request_info.promise.get_future();
}
