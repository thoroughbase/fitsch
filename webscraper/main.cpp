#include <string>

#include "webscraper/app.hpp"
#include "webscraper/stores.hpp"

#include "common/util.hpp"

using std::string;

int main(int argc, char** argv)
{
    App a(argc > 1 ? argv[1] : "config.json");
    a.AddStore(&stores::SuperValu);
    a.AddStore(&stores::Tesco);

    string url = "https://shop.supervalu.ie/sm/delivery/rsid/5550/"
    			 "product/batchelors-chick-peas-225-g-id-1018033000";
    a.GetProductAtURL(StoreID::SUPERVALU, url);

    string url2 = "https://www.tesco.ie/groceries/en-IE/products/303007973";
    a.GetProductAtURL(StoreID::TESCO, url2);

    string input;
    while (1) {
        std::getline(std::cin, input);
        if (input == "quit") break;
    }

    return 0;
}
