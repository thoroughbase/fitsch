#include <string>

#include "webscraper/app.hpp"
#include "webscraper/stores.hpp"

#include "common/util.hpp"

int main(int argc, char** argv)
{
    Log(LogLevel::INFO, "Starting Fitsch {}", FITSCH_VERSION);

    std::string_view cfg_path = argc > 1 ? argv[1] : "config.json";

    std::optional<AppConfig> config = AppConfig::FromJSONFile(cfg_path);
    if (!config) {
        Log(LogLevel::SEVERE, "Couldn't read config, exiting");
        return 1;
    }

    App a(config.value());

    a.AddStore(&stores::SuperValu);
    a.AddStore(&stores::Tesco);
    a.AddStore(&stores::DunnesStores);
    a.AddStore(&stores::Aldi);

    std::string_view url = "https://shop.supervalu.ie/sm/delivery/rsid/5550/"
    			 	  	   "product/batchelors-chick-peas-225-g-id-1018033000";
    a.GetProductAtURL(StoreID::SUPERVALU, url);

    std::string_view url2 = "https://www.tesco.ie/groceries/en-IE/products/303007973";
    a.GetProductAtURL(StoreID::TESCO, url2);

    std::string_view url3
    	= "https://www.dunnesstoresgrocery.com/sm/delivery/rsid/258/"
          "product/dunnes-stores-irish-chicken-breast-fillets-840g-id-100222328";
    a.GetProductAtURL(StoreID::DUNNES_STORES, url3);

    std::string input;
    while (1) {
        std::getline(std::cin, input);
        if (input == "quit") break;
    }

    return 0;
}
