#include <string>

#include <fmt/core.h>
#include <fmt/format.h>

#include "webscraper/app.hpp"
#include "webscraper/stores.hpp"

#include "common/util.hpp"

using std::string;

int main(int argc, char** argv)
{
    App a(argc > 1 ? argv[1] : "config.json");
    a.AddStore(&stores::SuperValu);

    string url =
        "https://shop.supervalu.ie/sm/delivery/rsid/5550/product/batchelors-chick-peas-225-g-id-1018033000";
    a.GetProductAtURL(StoreID::SUPERVALU, url);

    while (1) {
        string str;
        std::getline(std::cin, str);
        if (str == "quit") break;

        size_t ss = 0;
        while (ss != string::npos && str.size()) {
            ss = str.find(",");
            string query = str.substr(0, ss);
            if (!query.empty()) {
                Log(INFO, "Running query {}", query);
                a.DoQuery(StoreID::SUPERVALU, query, 15);
            }
            if (ss != string::npos) str = str.substr(ss + 1);
        }
    }

    return 0;
}
