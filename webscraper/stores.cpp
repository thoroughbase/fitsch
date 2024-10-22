#include "webscraper/stores.hpp"

#include <vector>
#include <iostream>

#include "common/util.hpp"

// SuperValu

Product SV_GetProductAtURL(const HTML& html)
{
    Collection<Element> metatags = html.SearchTag("meta", ELEMENT_HEAD);

    Product result;
    result.store = stores::SuperValu.id;
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
            result.item_price = content;
        }
    }

    Collection<Element> priceper = html.SearchClass("PdpUnitPrice-", ELEMENT_BODY, true);

    if (!priceper.size()) result.price_per_unit = { Unit::Piece, result.item_price };
    else {
        Element e = priceper[0];
        result.price_per_unit = e.FirstChild().Text();
    }

    return result;
}

ProductList SV_Search(const string& query, CURL* curl, int depth)
{
    string base_url = stores::SuperValu.homepage + "results?q=" + query;
    ProductList results(depth);
    results.reserve(depth > 0 ? depth : 30);

    Collection<Element> item_listings, name_c, price_c, price_per_c, image_c, url_c;

    int max_per_page = 30, skip = 0;
    int page_items;

    HTML html;

    do {
        string url = base_url + "&skip=" + std::to_string(skip);
        page_items = 0;

        html.ReadFromURL(curl, url);

        html.SearchClass(item_listings, "ColListing", ELEMENT_BODY, true);
        if (!(page_items = item_listings.size())) break;

        skip += max_per_page;
        int i = 0;
        for (Element e : item_listings) {
            Product product;
            product.store = stores::SuperValu.id;

            html.SearchAttr(name_c, "data-testid", "ProductNameTestId", e, true);
            html.SearchClass(price_c, "ProductCardPrice-", e, true);
            html.SearchClass(price_per_c, "ProductCardPriceInfo", e, true);
            html.SearchClass(image_c, "ProductCardImage-", e, true);
            html.SearchClass(url_c, "ProductCardHiddenLink", e, true);

            if (!name_c.size() || !price_c.size() || !price_per_c.size()
             || !image_c.size() || !url_c.size()) {
                Log(WARNING, "One or more details missing for product {} on page {}",
                    i, url);
                continue;
            }

            Node node = name_c[0].FirstChild();
            string namestr;
            do {
                if ((namestr = node.Text()) != BLANK) break;
            } while ((node = node.Next()));

            if (namestr == BLANK) {
                Log(WARNING, "Name not found for product {} on page {}", i, url);
                continue;
            }

            string str_id = name_c[0].GetAttrValue("data-testid");
            str_id = str_id.substr(0, str_id.find('-'));

            product.name = namestr;
            product.item_price = price_c[0].FirstChild().Text();
            product.price_per_unit = price_per_c[0].FirstChild().Text();
            product.id = stores::SuperValu.prefix + str_id;
            product.url = url_c[0].GetAttrValue("href");
            product.image_url = image_c[0].GetAttrValue("src");
            product.timestamp = std::time(nullptr);

            if (product.price_per_unit.first == Unit::None) {
                product.price_per_unit.first = Unit::Piece;
                product.price_per_unit.second = product.item_price;
            }

            results.emplace_back(product, QueryResultInfo { (int) results.size() });

            if (results.size() >= depth) break;

            ++i;
        }

        if (results.size() >= depth) break;
    } while (page_items >= max_per_page);
    return results;
}
