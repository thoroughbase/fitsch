#include "webscraper/stores.hpp"

#include <curl/curl.h>

#include "common/util.hpp"

// SuperValu

Product SV_GetProductAtURL(const HTML& html)
{
    Collection<Element> metatags = html.SearchTag("meta", ELEMENT_HEAD);

    Product result { .store = stores::SuperValu.id };

    for (Element e : metatags) {
        if (!e.HasAttr("itemprop")) continue;

        string content = e.HasAttr("content") ? e.GetAttrValue("content") : "";

        string property = e.GetAttrValue("itemprop");
        if (property == "name") {
            result.name = content;
        } else if (property == "image") {
            result.image_url = e.GetAttrValue("href");
        } else if (property == "description") {
            result.description = content;
        } else if (property == "sku") {
            result.id = stores::SuperValu.prefix + content;
        } else if (property == "price") {
            result.item_price = Price::FromString(content);
        }
    }

    Collection<Element> priceper = html.SearchClass("PdpUnitPrice-", ELEMENT_BODY, true);

    if (!priceper.size()) {
        result.price_per_unit.unit = Unit::Piece;
        result.price_per_unit.price = result.item_price;
    } else {
        Element e = priceper[0];
        result.price_per_unit = PricePU::FromString(e.FirstChild().Text());
    }

    return result;
}

string SV_GetProductSearchURL(std::string_view query_string)
{
    char* buffer = curl_easy_escape(nullptr, query_string.data(), query_string.size());
    if (!buffer) {
        Log(WARNING, "Failed to escape query string {}", query_string);
        return {};
    }

    std::string url = fmt::format("{}/results?q={}&skip=0",
        stores::SuperValu.homepage, buffer);

    curl_free(buffer);

    return url;
}

ProductList SV_ParseProductSearch(std::string_view data, int depth)
{
    // TODO: Reimplement reading multiple pages

    ProductList results(depth);
    results.products.reserve(depth > 0 ? depth : 30);

    Collection<Element> item_listings, name_c, price_c, price_per_c, image_c, url_c;
    int page_items = 0;

    HTML html(data);
    html.SearchClass(item_listings, "ColListing", ELEMENT_BODY, true);

    int i = 0;
    for (Element e : item_listings) {
        Product product { .store = stores::SuperValu.id };

        html.SearchAttr(name_c, "data-testid", "ProductNameTestId", e, true);
        html.SearchClass(price_c, "ProductCardPrice-", e, true);
        html.SearchClass(price_per_c, "ProductCardPriceInfo", e, true);
        html.SearchClass(image_c, "ProductCardImage-", e, true);
        html.SearchClass(url_c, "ProductCardHiddenLink", e, true);

        if (!name_c.size() || !price_c.size() || !price_per_c.size()
         || !image_c.size() || !url_c.size()) {
            Log(WARNING, "One or more details missing for product {} on page", i);
            continue;
        }

        Node node = name_c[0].FirstChild();
        string namestr;
        do {
            if ((namestr = node.Text()) != BLANK) break;
        } while ((node = node.Next()));

        if (namestr == BLANK) {
            Log(WARNING, "Name not found for product {}", i);
            continue;
        }

        string str_id = name_c[0].GetAttrValue("data-testid");
        str_id = str_id.substr(0, str_id.find('-'));

        product.name = namestr;
        product.item_price = Price::FromString(price_c[0].FirstChild().Text());
        product.price_per_unit =
            PricePU::FromString(price_per_c[0].FirstChild().Text());
        product.id = stores::SuperValu.prefix + str_id;
        product.url = url_c[0].GetAttrValue("href");
        product.image_url = image_c[0].GetAttrValue("src");
        product.timestamp = std::time(nullptr);

        if (product.price_per_unit.unit == Unit::None) {
            product.price_per_unit.unit = Unit::Piece;
            product.price_per_unit.price = product.item_price;
        }

        results.products.emplace_back(std::move(product),
            QueryResultInfo { (int) results.products.size() });

        if (results.products.size() >= depth) break;

        ++i;
    }

    return results;
}
