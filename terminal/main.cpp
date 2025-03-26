#include <iostream>
#include <string>
#include <vector>

#include <buxtehude/buxtehude.hpp>
#include <buxtehude/validate.hpp>

#include <nlohmann/json.hpp>
#include <fmt/core.h>

#include "common/validate.hpp"
#include "common/product.hpp"

using nlohmann::json;

std::vector<std::string> split(std::string_view view, char delim)
{
    std::vector<std::string> result;

    std::string_view::iterator iterator;
    while ((iterator = std::find(view.begin(), view.end(), delim)) != view.end()) {
        if (iterator - view.begin() > 0) result.emplace_back(view.begin(), iterator);
        view.remove_prefix(iterator - view.begin() + 1);
    }

    if (!view.empty()) result.emplace_back(view);

    return result;
}

int main()
{
    namespace bux = buxtehude;

    bux::Initialise();

    bux::Server server;
    server.InternalServer().if_err([] (bux::AllocError) {
        fmt::print("Failed to start buxtehude server, exiting...\n");
        std::exit(1);
    });

    server.IPServer(1637).if_err([] (bux::ListenError) {
        fmt::print("Failed to start buxtehude INET server, exiting...\n");
        std::exit(1);
    });

    bux::Client terminal({
        .teamname = "terminal",
    });

    terminal.AddHandler("query-result", [] (bux::Client& cl, const bux::Message& m) {
        if (!bux::ValidateJSON(m.content, validate::QUERY_RESULT)) return;
        fmt::print("Query results ({}):\n", m.content["term"].get<std::string>());

        for (const json& j : m.content["items"]) {
            if (!j.contains("name")) continue;
            fmt::print("  {}\n", j["name"].get<std::string>());
        }
    });

    terminal.InternalConnect(server).ignore_error();

    std::string input;

    while (1) {
        std::getline(std::cin, input);
        if (input.empty()) continue;
        if (input == "quit") break;

        std::vector<std::string> terms = split(input, ',');
        auto stores = json::array({
            StoreID::SUPERVALU, StoreID::DUNNES_STORES, StoreID::TESCO, StoreID::ALDI
        });

        terminal.Write({
            .type = "query", .dest = "webscraper", .only_first = true,
            .content = {
                { "terms", terms },
                { "request-id", 0 },
                { "stores", std::move(stores) },
                { "depth", 10 }
            }
        }).ignore_error();
    }

    return 0;
}
