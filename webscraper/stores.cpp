#include "webscraper/stores.hpp"

#include <curl/curl.h>

#include "common/util.hpp"

// SuperValu

std::optional<Product> SV_GetProductAtURL(const HTML& html)
{
    Collection<Element> metatags = html.SearchTag("meta", ELEMENT_HEAD);

    Product result {
        .store = stores::SuperValu.id,
        .timestamp = std::time(nullptr),
    };

    for (Element e : metatags) {
        if (!e.HasAttr("itemprop")) continue;

        std::string_view content = e.HasAttr("content") ? e.GetAttrValue("content") : "";
        std::string_view property = e.GetAttrValue("itemprop");

        if (property == "name") {
            result.name = content;
        } else if (property == "image") {
            result.image_url = e.GetAttrValue("href");
        } else if (property == "description") {
            result.description = content;
        } else if (property == "sku") {
            result.id = fmt::format("{}{}", stores::SuperValu.prefix, content);
            result.url = fmt::format("{}/product/{}", stores::SuperValu.homepage,
                                     content);
        } else if (property == "price") {
            result.item_price = Price::FromString(string(content));
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

        Node name_text_node = name_c[0].FindChildIf([] (const Node n) {
            return !n.Text().empty();
        });

        if (!name_text_node) {
            Log(WARNING, "Name not found for product {}", i);
            continue;
        }

        std::string_view str_id = name_c[0].GetAttrValue("data-testid");
        str_id.remove_suffix(str_id.size() - str_id.find('-'));

        Product product {
            .store = stores::SuperValu.id,
            .name { name_text_node.Text() },
            .item_price = Price::FromString(string(price_c[0].FirstChild().Text())),
            .price_per_unit = PricePU::FromString(price_per_c[0].FirstChild().Text()),
            .id = fmt::format("{}{}", stores::SuperValu.prefix, str_id),
            .url { url_c[0].GetAttrValue("href") },
            .image_url { image_c[0].GetAttrValue("src") },
            .timestamp = std::time(nullptr)
        };

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

// Tesco

ProductList TE_ParseProductSearch(std::string_view data, int depth)
{
    HTML html(data);
    Collection<Element> item_listings = html.SearchClass("product-list--list-item",
        ELEMENT_BODY, true);

    ProductList results(depth);
    Collection<Element> title_c, name_c, image_div_c, image_c, price_c, price_per_c;
    for (Element e : item_listings) {
        html.SearchAttr(title_c, "data-auto", "product-tile--title", e, true);
        html.SearchClass(image_div_c, "product-image__container", e, true);

        if (!title_c.size() || !image_div_c.size())
           continue;

        html.SearchTag(name_c, "span", title_c[0]);
        html.SearchTag(image_c, "img", image_div_c[0]);
        html.SearchClass(price_c, "beans-price__text", e, true);
        html.SearchClass(price_per_c, "beans-price__subtext", e, true);

        if (!name_c.size() || !image_c.size() || !price_c.size() || !price_per_c.size())
            continue;

        std::string_view relative_url = title_c[0].GetAttrValue("href");
        std::string_view id = relative_url;
        id.remove_prefix(relative_url.rfind('/') + 1);

        std::string_view srcset = image_c[0].GetAttrValue("srcset");
        srcset.remove_suffix(srcset.size() - srcset.find(' ') - 1);

        std::string_view ppu_view = price_per_c[0].FirstChild().Text();
        auto end_substr = ppu_view.find(' ');
        if (end_substr != string::npos)
            ppu_view.remove_suffix(ppu_view.size() - end_substr);

        Product product = {
            .store = stores::Tesco.id,
            .full_info = false,
            .name { name_c[0].FirstChild().Text() },
            .id = fmt::format("{}{}", stores::Tesco.prefix, id),
            .url = fmt::format("{}{}", stores::Tesco.root_url, relative_url),
            .image_url { srcset },
            .item_price = Price::FromString(string(price_c[0].FirstChild().Text())),
            .price_per_unit = PricePU::FromString(ppu_view)
        };

        results.products.emplace_back(std::move(product),
            QueryResultInfo { (int) results.products.size() });

        if (depth != SEARCH_DEPTH_INDEFINITE && results.products.size() >= depth) break;
    }

    return results;
}

string TE_GetProductSearchURL(std::string_view query)
{
    char* buffer = curl_easy_escape(nullptr, query.data(), query.size());
    if (!buffer) {
        Log(WARNING, "Failed to escape query string {}", query);
        return {};
    }

    string url = fmt::format("{}/search?query={}", stores::Tesco.homepage, buffer);
    free(buffer);

    return url;
}

std::optional<Product> TE_GetProductAtURL(const HTML& html)
{
    Collection<Element> product_json = html.SearchAttr("type", "application/ld+json",
        ELEMENT_HEAD, false);

    if (!product_json.size()) {
        Log(WARNING, "Product information not found for Tesco product page - "
                     "element not found");
        return {};
    }

    json root_json_obj = json::parse(product_json[0].FirstChild().Text());
    json& graph = root_json_obj["@graph"];
    auto iterator = std::find_if(graph.begin(), graph.end(), [] (const json& j) {
        return j["@type"].get<string>() == "Product";
    });

    if (iterator == graph.end()) {
        Log(WARNING, "Product information not found for Tesco product page - "
                     "JSON obj not found");
        return {};
    }

    json& product_info = *iterator;

    unsigned int price = product_info["offers"]["price"].get<float>() * 100;

    const std::string sku = product_info["sku"].get<string>();

    Product result = {
        .store = stores::Tesco.id, .name = product_info["name"],
        .description = product_info["description"],
        .id = fmt::format("{}{}", stores::Tesco.prefix, sku),
        .image_url = product_info["image"][0],
        .item_price = Price { EUR, price },
        .timestamp = std::time(nullptr),
        .url = fmt::format("{}/products/{}", stores::Tesco.homepage, sku)
    };

    Collection<Element> priceper = html.SearchClass("ddsweb-price__subtext",
        ELEMENT_BODY, true);

    if (!priceper.size()) {
        result.price_per_unit = PricePU { result.item_price, Unit::Piece };
    } else {
        std::string_view ppu_view = priceper[0].FirstChild().Text();
        auto end_substr = ppu_view.find(' ');
        if (end_substr != string::npos)
            ppu_view.remove_suffix(ppu_view.size() - end_substr);

        result.price_per_unit = PricePU::FromString(ppu_view);
    }

    return result;
}
