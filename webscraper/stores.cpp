#include "webscraper/stores.hpp"

#include <curl/curl.h>

#include "common/util.hpp"

// SuperValu-like: See stores.md

std::optional<Product> SVLike_GetProductAtURL(const Store& store, const HTML& html)
{
    Collection<Element> metatags = html.SearchTag("meta", Element::HEAD);

    Product result {
        .store = store.id,
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
            result.id = fmt::format("{}{}", store.prefix, content);
            result.url = fmt::format("{}/product/{}", store.homepage,
                                     content);
        } else if (property == "price") {
            std::optional<Price> price = Price::FromString(content);
            if (!price)
                return std::nullopt;
            result.item_price = price.value();
        }
    }

    Collection<Element> priceper = html.SearchClass("PdpUnitPrice-", Element::BODY, true);

    if (!priceper.size()) {
        result.price_per_unit.unit = Unit::Piece;
        result.price_per_unit.price = result.item_price;
    } else {
        Element e = priceper[0];
        result.price_per_unit
            = PricePU::FromString(e.FirstChild().Text()).value_or(PricePU {
            .price = result.item_price,
            .unit = Unit::Piece
        });
    }

    return result;
}

std::string SVLike_GetProductSearchURL(const Store& store, std::string_view query_string)
{
    char* buffer = curl_easy_escape(nullptr, query_string.data(), query_string.size());
    if (!buffer) {
        Log(LogLevel::WARNING, "Failed to escape query string {}", query_string);
        return {};
    }

    tb::scoped_guard free_buffer = [buffer] { curl_free(buffer); };
    return fmt::format("{}/results?q={}&skip=0", store.homepage, buffer);
}

ProductList SVLike_ParseProductSearch(const Store& store, std::string_view data,
                                      size_t depth)
{
    // TODO: Reimplement reading multiple pages
    std::optional<HTML> html_opt = HTML::FromString(data);
    if (!html_opt) {
        Log(LogLevel::WARNING, "Failed to parse HTML!");
        return {};
    }

    HTML& html = html_opt.value();

    Collection<Element> item_listings
        = html.SearchClass("ColListing", Element::BODY, true);

    ProductList results(depth);
    results.products.reserve(item_listings.size());

    Collection<Element> name_c, price_c, price_per_c, image_c, url_c;
    for (Element e : item_listings) {
        html.SearchAttr(name_c, "data-testid", "ProductNameTestId", e, true);
        html.SearchClass(price_c, "ProductCardPrice-", e, true);
        html.SearchClass(price_per_c, "ProductCardPriceInfo", e, true);
        html.SearchClass(image_c, "ProductCardImage-", e, true);
        html.SearchClass(url_c, "ProductCardHiddenLink", e, true);

        // For Dunnes Stores - 24/4/25
        if (!price_c.size())
            html.SearchClass(price_c, "ProductPrice-", e, true);

        if (!price_per_c.size())
            html.SearchClass(price_per_c, "ProductUnitPrice-", e, true);

        if (!image_c.size())
            html.SearchClass(image_c, "ProductImage-", e, true);

        if (!name_c.size() || !price_c.size() || !image_c.size() || !url_c.size()) {
            Log(LogLevel::WARNING,
                "Incomplete product info for product #{} (Store: {})\n"
                "  Name: {}, Price: {}, Price Per: {}, Image: {}, URL: {}\n",
                results.products.size(), store.name,
                name_c.size() > 0, price_c.size() > 0, price_per_c.size() > 0,
                image_c.size() > 0, url_c.size() > 0);
            continue;
        }

        Node name_text_node = name_c[0].FindChildIf([] (Node n) {
            return !n.Text().empty();
        });

        if (!name_text_node) {
            Log(LogLevel::WARNING,
                "Name not found for product #{} (Store: {})",
                results.products.size(), store.name);
            continue;
        }

        std::optional<Price> price = Price::FromString(price_c[0].FirstChild().Text());
        if (!price) {
            Log(LogLevel::WARNING,
                "Couldn't parse price string for product #{} (Store: {})\n"
                "  string: {}",
                results.products.size(), store.name, price_c[0].FirstChild().Text());
            continue;
        }

        std::string_view str_id = name_c[0].GetAttrValue("data-testid");
        str_id.remove_suffix(str_id.size() - str_id.find('-'));

        Collection<Element> offers_html;

        if (store.id == StoreID::SUPERVALU) {
            html.SearchAttr(offers_html, "data-testid",
                "promotionBadgeComponent", e, true);
        } else {
            Collection<Element> card_charges
                = html.SearchAttr("data-testid", "cardCharges", e, true);
            if (card_charges.size())
                html.SearchClass(offers_html, "PromotionLabelBadge",
                    card_charges[0], true);
        }

        std::vector<Offer> offers_vec;
        offers_vec.reserve(offers_html.size());
        for (Element offer : offers_html) {
            if (std::optional<Offer> opt = Offer::FromString(offer.FirstChild().Text()))
                offers_vec.emplace_back(std::move(opt.value()));
        }

        Product product {
            .name { name_text_node.Text() },
            .image_url { image_c[0].GetAttrValue("src") },
            .url { url_c[0].GetAttrValue("href") },
            .id = fmt::format("{}{}", store.prefix, str_id),
            .offers = std::move(offers_vec),
            .item_price = price.value(),
            .store = store.id,
            .price_per_unit = {},
            .timestamp = std::time(nullptr),
            .full_info = false
        };

        if (price_per_c.size()) {
            product.price_per_unit
                = PricePU::FromString(price_per_c[0].FirstChild().Text())
                  .value_or(PricePU {
                .price = product.item_price,
                .unit = Unit::Piece
            });
        }

        if (product.price_per_unit.unit == Unit::None) {
            product.price_per_unit.unit = Unit::Piece;
            product.price_per_unit.price = product.item_price;
        }

        results.products.emplace_back(std::move(product),
            QueryResultInfo { results.products.size() });

        if (results.products.size() >= depth) break;
    }

    return results;
}

CURLOptions Default_GetProductSearchCURLOptions(std::string_view query)
{
    return {};
}

// SuperValu

std::optional<Product> SV_GetProductAtURL(const HTML& html)
{
    return SVLike_GetProductAtURL(stores::SuperValu, html);
}

std::string SV_GetProductSearchURL(std::string_view query_string)
{
    return SVLike_GetProductSearchURL(stores::SuperValu, query_string);
}

ProductList SV_ParseProductSearch(std::string_view data, size_t depth)
{
    return SVLike_ParseProductSearch(stores::SuperValu, data, depth);
}

// Dunnes Stores

ProductList DS_ParseProductSearch(std::string_view data, size_t depth)
{
    return SVLike_ParseProductSearch(stores::DunnesStores, data, depth);
}

std::string DS_GetProductSearchURL(std::string_view query)
{
    return SVLike_GetProductSearchURL(stores::DunnesStores, query);
}

std::optional<Product> DS_GetProductAtURL(const HTML& html)
{
    return SVLike_GetProductAtURL(stores::DunnesStores, html);
}

// Tesco

ProductList TE_ParseProductSearch(std::string_view data, size_t depth)
{
    std::optional<HTML> html_opt = HTML::FromString(data);
    if (!html_opt) {
        Log(LogLevel::WARNING, "Failed to parse HTML!");
        return {};
    }

    HTML& html = html_opt.value();
    Collection<Element> item_listings = html.SearchClass("WL_DZ",
        Element::BODY, true);

    ProductList results(depth);
    results.products.reserve(item_listings.size());
    Collection<Element> name_c, image_c, price_c, price_per_c;
    for (Element e : item_listings) {
        html.SearchClass(name_c, "titleContainer", e, true);
        html.SearchClass(image_c, "baseImage", e, true);
        html.SearchClass(price_c, "_priceText", e, true);
        html.SearchClass(price_per_c, "price__subtext", e, true);

        if (!name_c.size() || !image_c.size() || !price_c.size() || !price_per_c.size())
            continue;

        std::string_view id_text = e.GetAttrValue("data-testid");

        std::optional<Price> price
            = Price::FromString(price_c[0].FirstChild().Text(true));
        std::optional<PricePU> price_per
            = PricePU::FromString(price_per_c[0].FirstChild().Text(true));

        if (!price) {
            Log(LogLevel::WARNING, "Failed to parse price '{}'",
                price_c[0].FirstChild().Text());
            continue;
        }

        if (!price_per) {
            Log(LogLevel::WARNING, "Failed to parse price per unit '{}'",
                price_per_c[0].FirstChild().Text());
            continue;
        }

        Product product {
            .name { name_c[0].FirstChild().Text(true) },
            .image_url { image_c[0].GetAttrValue("src") },
            .url = fmt::format("{}/products/{}", stores::Tesco.homepage, id_text),
            .id = fmt::format("{}{}", stores::Tesco.prefix, id_text),
            .offers = {},
            .item_price = price.value(),
            .store = stores::Tesco.id,
            .price_per_unit = price_per.value(),
            .timestamp = std::time(nullptr),
            .full_info = false
        };

        results.products.emplace_back(std::move(product),
            QueryResultInfo { results.products.size() });

        if (results.products.size() >= depth) break;
    }

    return results;
}

std::string TE_GetProductSearchURL(std::string_view query)
{
    char* buffer = curl_easy_escape(nullptr, query.data(), query.size());
    if (!buffer) {
        Log(LogLevel::WARNING, "Failed to escape query string {}", query);
        return {};
    }

    tb::scoped_guard free_buffer = [buffer] { curl_free(buffer); };
    return fmt::format("{}/search?query={}", stores::Tesco.homepage, buffer);
}

std::optional<Product> TE_GetProductAtURL(const HTML& html)
{
    Collection<Element> product_json = html.SearchAttr("type", "application/ld+json",
        Element::HEAD, false);

    if (!product_json.size()) {
        Log(LogLevel::WARNING, "Product information not found for Tesco product page - "
                     "element not found");
        return {};
    }

    json root_json_obj;

    try {
         root_json_obj = json::parse(product_json[0].FirstChild().Text());
    } catch (const json::parse_error& e) {
        Log(LogLevel::WARNING, "Failed to parse Tesco product info: {}", e.what());
        return {};
    }

    json& graph = root_json_obj["@graph"];
    auto iterator = std::find_if(graph.begin(), graph.end(), [] (const json& j) {
        return j["@type"].get<std::string_view>() == "Product";
    });

    if (iterator == graph.end()) {
        Log(LogLevel::WARNING,
            "Product information not found for Tesco product page - JSON obj not found");
        return {};
    }

    json& product_info = *iterator;

    unsigned int price = product_info["offers"]["price"].get<float>() * 100;

    auto sku = product_info["sku"].get<std::string_view>();

    Product result = {
        .name = product_info["name"],
        .description = product_info["description"],
        .image_url = product_info["image"][0],
        .url = fmt::format("{}/products/{}", stores::Tesco.homepage, sku),
        .id = fmt::format("{}{}", stores::Tesco.prefix, sku),
        .item_price = Price { Currency::EUR, price },
        .store = stores::Tesco.id,
        .timestamp = std::time(nullptr),
        .full_info = true
    };

    Collection<Element> priceper = html.SearchClass("ddsweb-price__subtext",
        Element::BODY, true);

    if (!priceper.size()) {
        result.price_per_unit = PricePU { result.item_price, Unit::Piece };
    } else {
        std::string_view ppu_view = priceper[0].FirstChild().Text();
        auto end_substr = ppu_view.find(' ');
        if (end_substr != std::string::npos)
            ppu_view.remove_suffix(ppu_view.size() - end_substr);

        result.price_per_unit = PricePU::FromString(ppu_view).value_or(PricePU {
            .price = result.item_price,
            .unit = Unit::Piece
        });
    }

    return result;
}

// Aldi

ProductList AL_ParseProductSearch(std::string_view data, size_t depth)
{
    json json_obj;
    try {
        json_obj = json::parse(data);
    } catch (const json::parse_error& e) {
        Log(LogLevel::WARNING, "Failed to parse Aldi response: {}", e.what());
        return {};
    }

    constexpr std::string_view BASE_IMAGE_URL
        = "https://dm.emea.cms.aldi.cx/is/image/aldiprodeu/"
          "product/jpg/scaleWidth/1296";
    constexpr std::string_view FALLBACK_IMAGE_URL
        = "https://dm.emea.cms.aldi.cx/is/content/aldiprodeu/"
          "GB%20Fallback%20Image%203-no%20text";

    json& items = json_obj["data"];
    ProductList results;
    results.products.reserve(items.size());
    size_t current_item = 0;
    for (json& item : items) {
        ++current_item;
        try {
            std::string_view brand_name
                = item["brandName"].is_string()
                ? item["brandName"].get<std::string_view>() : "";

            Product product {
                .name = fmt::format("{} {}", brand_name,
                                    item["name"].get<std::string_view>()),
                .description = {},
                .url = fmt::format("{}/product/{}", stores::Aldi.root_url,
                    item["sku"].get<std::string_view>()),
                .id = fmt::format("{}{}", stores::Aldi.prefix,
                    item["sku"].get<std::string_view>()),
                .item_price = Price {
                    Currency::EUR, item["price"]["amount"].get<unsigned>()
                },
                .store = stores::Aldi.id,
                .timestamp = std::time(nullptr),
                .full_info = false
            };

            if (!item["assets"].empty()) {
                auto product_image_id = item["assets"][0]["url"].get<std::string_view>();
                constexpr size_t suffix_size = std::string_view("\\/{slug}").size();
                product_image_id.remove_suffix(suffix_size - 1);
                size_t image_id_start = product_image_id.rfind('/');
                product_image_id.remove_prefix(image_id_start + 1);

                product.image_url = fmt::format("{}/{}", BASE_IMAGE_URL,
                    product_image_id);
            } else {
                product.image_url = FALLBACK_IMAGE_URL;
            }

            // Parsing this string as a PricePU will yield the correct unit, but
            // not the price. The actual price per unit is in /price/comparison

            if (item["sellingSize"].is_string()
                && item.contains("/price/comparison"_json_pointer)
                && item["/price/comparison"_json_pointer].is_number()) {
                product.price_per_unit
                    = PricePU::FromString(item["sellingSize"].get<std::string_view>())
                      .value_or(PricePU {
                    .price = product.item_price,
                    .unit = Unit::Piece
                });
                product.price_per_unit.price = {
                    Currency::EUR,
                    item["price"]["comparison"].get<unsigned>()
                };
            } else {
                product.price_per_unit.unit = Unit::Piece;
                product.price_per_unit.price = product.item_price;
            }

            results.products.emplace_back(std::move(product),
                QueryResultInfo { results.products.size() });
            if (results.products.size() >= depth)
                break;
        } catch (const json::exception& e) {
            Log(LogLevel::WARNING, "Invalid JSON for item #{} whilst parsing"
                " ALDI query: {}", current_item, e.what());
        }
    }

    return results;
}

std::string AL_GetProductSearchURL(std::string_view query)
{
    char* buffer = curl_easy_escape(nullptr, query.data(), query.size());
    if (!buffer) {
        Log(LogLevel::WARNING, "Failed to escape query string {}", query);
        return {};
    }

    tb::scoped_guard free_buffer = [buffer] { curl_free(buffer); };

    return fmt::format(
        "https://api.aldi.ie/v3/product-search?&q={}&limit=30&sort=relevance",
        buffer
    );
}

std::optional<Product> AL_GetProductAtURL(const HTML& html)
{
    // TODO: Implement
    return {};
}

CURLOptions AL_GetProductSearchCURLOptions(std::string_view query)
{
    return {
        .headers = &CURLHEADERS_ALDI,
        .method = CURLOptions::Method::GET
    };
}
