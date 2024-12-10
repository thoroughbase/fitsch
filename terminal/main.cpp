#include <iostream>
#include <string>
#include <vector>

#include <buxtehude/buxtehude.hpp>
#include <buxtehude/validate.hpp>

#include <nlohmann/json.hpp>
#include <fmt/core.h>

using nlohmann::json;

const buxtehude::ValidationSeries VALIDATE_QUERY_RESULT = {
    { "/term"_json_pointer, buxtehude::predicates::NotEmpty },
    { "/items"_json_pointer, [] (const json& j) { return j.is_array(); } }
};

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
    using namespace buxtehude;

    buxtehude::Initialise();

    Server server;
    server.IPServer(1637);
    server.Run();

    Client terminal(server, "terminal");

    terminal.AddHandler("query-result", [] (Client& cl, const Message& m) {
        if (!ValidateJSON(m.content, VALIDATE_QUERY_RESULT)) return;
        fmt::print("Query results ({}):\n", m.content["term"].get<std::string>());

        for (const json& j : m.content["items"]) {
            if (!j.contains("name")) continue;
            fmt::print("  {}\n", j["name"].get<std::string>());
        }
    });

    terminal.Run();

    std::string input;

    while (1) {
        std::getline(std::cin, input);
        if (input.empty()) continue;
        if (input == "quit") break;

        std::vector<std::string> terms = split(input, ',');

        terminal.Write({
            .type = "query", .dest = "webscraper", .only_first = true,
            .content = {
                { "terms", terms }
            }
        });
    }

    return 0;
}
