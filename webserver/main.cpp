#include <buxtehude/buxtehude.hpp>
#include <crow.h>
#include <curl/curl.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "common/util.hpp"
#include "webserver/queryhandler.hpp"

#include <chrono>
#include <ranges>

namespace bux = buxtehude;
using namespace std::chrono_literals;

void RetryConnection(bux::Client& client)
{
    constexpr static auto BASE_WAIT_TIME = 5s;
    constexpr static auto MAX_WAIT_TIME = 40s;
    static auto wait_time = BASE_WAIT_TIME;

    std::thread reconnect_thread([&client] {
        std::this_thread::sleep_for(wait_time);
        client.IPConnect("localhost", 1637).if_err([&client] (bux::ConnectError e) {
            Log(LogLevel::WARNING, "Failed to connect to buxtehude server: {}",
                e.What());
            if (wait_time < MAX_WAIT_TIME) wait_time += 5s;
            RetryConnection(client);
        }).if_ok([] {
            wait_time = BASE_WAIT_TIME;
            Log(LogLevel::INFO, "Reconnected to buxtehude server");
        });
    });
    reconnect_thread.detach();
}

std::string_view GetStoreLogo(StoreID store_id)
{
    switch (store_id) {
    case StoreID::SUPERVALU:     return "/static/images/logos/supervalu.png";
    case StoreID::LIDL:          return "/static/images/logos/lidl.png";
    case StoreID::TESCO:         return "/static/images/logos/tesco.png";
    case StoreID::ALDI:          return "/static/images/logos/aldi.png";
    case StoreID::DUNNES_STORES: return "/static/images/logos/dunnes.png";
    }
    return {};
}

std::string_view GetStoreName(StoreID store_id)
{
    switch (store_id) {
    case StoreID::SUPERVALU:     return "SuperValu";
    case StoreID::LIDL:          return "LIDL";
    case StoreID::TESCO:         return "Tesco";
    case StoreID::ALDI:          return "Aldi";
    case StoreID::DUNNES_STORES: return "Dunnes Stores";
    }
    return {};
}

int main()
{
    crow::SimpleApp crow_app;
    crow_app.loglevel(crow::LogLevel::Warning);

    bux::Initialise([] (bux::LogLevel level, std::string_view message) {
        if (level < bux::LogLevel::SEVERE) return;
        Log(static_cast<LogLevel>(level), "{}", message);
    });

    bux::Client bclient({
        .teamname = "webserver",
        .format = bux::MessageFormat::MSGPACK
    });

    bclient.IPConnect("localhost", 1637).if_err([&bclient] (bux::ConnectError e) {
        Log(LogLevel::WARNING, "Failed to connect to buxtehude server: {}, retrying...",
            e.What());
        RetryConnection(bclient);
    });

    bclient.SetDisconnectHandler([] (bux::Client& client) {
        Log(LogLevel::WARNING, "Connection dropped to buxtehude server, retrying...");
        RetryConnection(client);
    });

    QueryHandler query_handler(bclient, "webscraper");

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

        tb::scoped_guard free_curlstr = [curl_str] { curl_free(curl_str); };

        std::string non_lowercase(curl_str, unescaped_len);

        for (int i = 0; i < unescaped_len; ++i) curl_str[i] = tolower(curl_str[i]);

        std::string_view unescaped_term(curl_str, unescaped_len);

        std::future<QueryResultsMap> future = query_handler.SendQuery(unescaped_term);
        std::future_status status = future.wait_for(5s);

        if (status == std::future_status::timeout) {
            return crow::response("Timeout occurred");
        }

        QueryResultsMap result_map = future.get();
        std::vector<Product>& products = result_map.at(unescaped_term.data());

        std::ranges::sort(products,
            [] (auto& a, auto& b) {
                std::partial_ordering cmp = a.price_per_unit <=> b.price_per_unit;
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
                { "ppu", crow::json::wvalue(p.price_per_unit.ToString()) },
                { "url", crow::json::wvalue(std::move(p.url)) },
                { "logo_url", crow::json::wvalue(GetStoreLogo(p.store).data()) },
                { "store", crow::json::wvalue(GetStoreName(p.store).data()) },
                { "has_offers", crow::json::wvalue(p.offers.size() > 0) },
                { "offers", crow::json::wvalue(
                    p.offers | std::views::transform([] (const Offer& o) {
                        return o.ToString();
                    }) | tb::range_to<std::vector<crow::json::wvalue>>()
                ) }
            });
        }

        crow::mustache::context ctx {{
            { "term", std::move(non_lowercase) },
            { "item_listings", names },
            { "item_count", static_cast<uint64_t>(names.size()) }
        }};

        crow::mustache::template_t page = crow::mustache::load("results.html");
        return crow::response(page.render(ctx));
    });

    crow_app.port(8080).multithreaded().run();

    return 0;
}
