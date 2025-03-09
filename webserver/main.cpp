#include <buxtehude/buxtehude.hpp>
#include <crow.h>
#include <curl/curl.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "common/util.hpp"
#include "webserver/queryhandler.hpp"

#include <chrono>
#include <ranges>

int main()
{
    using namespace std::chrono_literals;
    crow::SimpleApp crow_app;
    crow_app.loglevel(crow::LogLevel::Warning);

    buxtehude::Initialise(
    [] (buxtehude::LogLevel level, std::string_view message) {
        Log((LogLevel) level, "{}", message);
    });

    auto bclient = std::make_unique<buxtehude::Client>();
    bclient->preferences.format = buxtehude::MSGPACK;

    if (!bclient->IPConnect("localhost", 1637, "webserver")) {
        Log(SEVERE, "Failed to connect to buxtehude server");
        return 1;
    }

    QueryHandler query_handler(*bclient.get(), "webscraper");

    bclient->Run();

    CROW_ROUTE(crow_app, "/")([] () {
        return crow::mustache::load("index.html").render();
    });

    CROW_ROUTE(crow_app, "/search/<string>")
    ([&query_handler] (const std::string& term) {
        int unescaped_len;
        char* curl_str = curl_easy_unescape(nullptr, term.data(), term.size(),
            &unescaped_len);
        if (!curl_str)
            return crow::response("Invalid search term - failed to unescape URL");

        std::string non_lowercase(curl_str, unescaped_len);

        for (int i = 0; i < unescaped_len; ++i) curl_str[i] = tolower(curl_str[i]);

        std::string_view unescaped_term(curl_str, unescaped_len);

        auto future = query_handler.SendQuery(unescaped_term);
        auto status = future.wait_for(5s);

        if (status == std::future_status::timeout) {
            return crow::response("Timeout occurred");
        }

        QueryResultsMap result_map = future.get();
        std::vector<Product>& products = result_map.at(unescaped_term.data());

        std::ranges::sort(products,
            [] (auto& a, auto& b) {
                auto cmp = a.price_per_unit <=> b.price_per_unit;
                if (cmp == std::partial_ordering::unordered)
                    return a.item_price < b.item_price;
                return a.price_per_unit < b.price_per_unit;
            }
        );

        crow::json::wvalue::list names;
        names.reserve(products.size());

        for (Product& p : products) {
            names.emplace_back(crow::json::wvalue {
                { "name", crow::json::wvalue(std::move(p.name)) },
                { "img", crow::json::wvalue(std::move(p.image_url)) },
                { "price", crow::json::wvalue(p.item_price.ToString()) },
                { "ppu", crow::json::wvalue(p.price_per_unit.ToString()) }
            });
        }

        crow::mustache::context ctx {{
            { "term", std::move(non_lowercase) },
            { "item_listings", names },
            { "item_count", static_cast<uint64_t>(names.size()) }
        }};

        curl_easy_cleanup(curl_str);
        auto page = crow::mustache::load("results.html");
        return crow::response(page.render(ctx));
    });

    crow_app.port(8080).multithreaded().run();

    return 0;
}
