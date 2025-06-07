#include <iostream>
#include <string>
#include <vector>

#include <buxtehude/buxtehude.hpp>
#include <buxtehude/validate.hpp>

#include <dflat/dflat.hpp>

#include <nlohmann/json.hpp>
#include <fmt/core.h>

#include "common/validate.hpp"
#include "common/product.hpp"
#include "common/util.hpp"

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

    bux::Initialise([] (bux::LogLevel level, std::string_view message) {
        Log(static_cast<LogLevel>(level), "(buxtehude) {}", message);
    });

    bux::Server server;

    auto quit_error = [&] { server.Close(); std::exit(1); };

    server.InternalServer().if_err([&] (bux::AllocError) {
        Log(LogLevel::SEVERE, "Failed to start buxtehude server\n");
        quit_error();
    });

    server.IPServer(1637).if_err([&] (bux::ListenError e) {
        Log(LogLevel::SEVERE, "Failed to start buxtehude INET server: {}\n", e.What());
        quit_error();
    });

    server.UnixServer(".fitsch_bux").if_err([&] (bux::ListenError e) {
        Log(LogLevel::SEVERE, "Failed to start buxtehude UNIX server: {}\n", e.What());
        if (e.type != bux::ListenErrorType::BIND_ERROR) {
            quit_error();
        }
    });

    bux::Client terminal({
        .teamname = "terminal"
    });

    bux::Client bux_database({
        .teamname = "dflat"
    });

    dflat::Database database(bux_database, "dflat");
    dflat::Handle db_handle(terminal, "dflat");

    terminal.AddHandler("query-result", [] (bux::Client& cl, const bux::Message& m) {
        if (!bux::ValidateJSON(m.content, validate::QUERY_RESULT)) return;
        fmt::print("Query results ({}):\n", m.content["term"].get<std::string>());

        for (const json& j : m.content["items"]) {
            if (!j.contains("name")) continue;
            fmt::print("  {}\n", j["name"].get<std::string>());
        }
    });

    terminal.InternalConnect(server).ignore_error();
    bux_database.InternalConnect(server).ignore_error();

    db_handle.Create("products", false, 2000).ignore_error();
    db_handle.Create("queries", false, 200).ignore_error();

    std::string input;

    while (1) {
        std::getline(std::cin, input);
        if (input.empty()) continue;
        if (input == "quit") break;

        std::vector<std::string> terms = split(input, ',');
        StoreSelection stores = StoreID::SUPERVALU | StoreID::DUNNES_STORES
                              | StoreID::TESCO | StoreID::ALDI;

        terminal.Write({
            .dest = "webscraper", .type = "query",
            .content = {
                { "terms", terms },
                { "request-id", 0 },
                { "stores", stores },
                { "depth", 10 },
                { "force-refresh", false }
            },
            .only_first = true,
        }).ignore_error();
    }

    return 0;
}
