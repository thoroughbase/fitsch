#include "webscraper/stores.hpp"

#include <curl/curl.h>

#include "common/util.hpp"

// SuperValu-like: See stores.md

ArenaProduct* SVLike_GetProductAtURL(const Store& store, const HTML& html,
    tb::thread_safe_memory_arena& arena)
{
    Collection<Element> metatags = html.SearchTag("meta", Element::HEAD);

    ArenaProduct& result
        = *arena.allocate_object<ArenaProduct>(ArenaProduct::WithArena(arena));

    result.store = store.id;
    result.timestamp = std::time(nullptr);

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
            result.id = std::format("{}{}", store.prefix, content);
            result.url = std::format("{}/product/{}", store.homepage,
                                     content);
        } else if (property == "price") {
            std::optional<Price> price = Price::FromString(content);
            if (!price)
                return nullptr;
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

    return &result;
}

std::string SVLike_GetProductSearchURL(const Store& store, std::string_view query_string)
{
    char* buffer = curl_easy_escape(nullptr, query_string.data(), query_string.size());
    if (!buffer) {
        Log(LogLevel::WARNING, "Failed to escape query string {}", query_string);
        return {};
    }

    tb::scoped_guard free_buffer = [buffer] { curl_free(buffer); };
    return std::format("{}/results?q={}&skip=0", store.homepage, buffer);
}

ArenaProductList* SVLike_ParseProductSearch(const Store& store, std::string_view data,
    tb::thread_safe_memory_arena& arena, size_t depth)
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

    ArenaProductList& results
        = *arena.allocate_object<ArenaProductList>(ArenaProductList::WithArena(arena));
    results.depth = depth;
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

        PMRProduct& pmr_product
            = *arena.allocate_object<PMRProduct>(ArenaProduct::WithArena(arena));

        ArenaProduct& product = std::get<ArenaProduct>(pmr_product);

        product.name = name_text_node.Text();
        product.image_url = image_c[0].GetAttrValue("src");
        product.url = url_c[0].GetAttrValue("href");
        product.id = std::format("{}{}", store.prefix, str_id);
        product.offers.reserve(offers_html.size());
        for (Element offer : offers_html) {
            if (auto opt = ArenaOffer::FromString(offer.FirstChild().Text(), &arena))
                product.offers.emplace_back(std::move(opt.value()));
        }
        product.item_price = price.value();
        product.store = store.id;
        product.price_per_unit = {};
        product.timestamp = std::time(nullptr);
        product.full_info = false;

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

        results.products.emplace_back(
            pmr_product,
            QueryResultInfo { results.products.size() }
        );

        if (results.products.size() >= depth) break;
    }

    return &results;
}

CURLOptions Default_GetProductSearchCURLOptions(std::string_view query)
{
    return {};
}

// SuperValu

ArenaProduct* SV_GetProductAtURL(const HTML& html, tb::thread_safe_memory_arena& arena)
{
    return SVLike_GetProductAtURL(stores::SuperValu, html, arena);
}

std::string SV_GetProductSearchURL(std::string_view query_string)
{
    return SVLike_GetProductSearchURL(stores::SuperValu, query_string);
}

ArenaProductList* SV_ParseProductSearch(std::string_view data, tb::thread_safe_memory_arena& arena, size_t depth)
{
    return SVLike_ParseProductSearch(stores::SuperValu, data, arena, depth);
}

// Dunnes Stores

ArenaProductList* DS_ParseProductSearch(std::string_view data, tb::thread_safe_memory_arena& arena, size_t depth)
{
    return SVLike_ParseProductSearch(stores::DunnesStores, data, arena, depth);
}

std::string DS_GetProductSearchURL(std::string_view query)
{
    return SVLike_GetProductSearchURL(stores::DunnesStores, query);
}

ArenaProduct* DS_GetProductAtURL(const HTML& html, tb::thread_safe_memory_arena& arena)
{
    return SVLike_GetProductAtURL(stores::DunnesStores, html, arena);
}

// Tesco

ArenaProductList* TE_ParseProductSearch(std::string_view data, tb::thread_safe_memory_arena& arena, size_t depth)
{
    std::optional<HTML> html_opt = HTML::FromString(data);
    if (!html_opt) {
        Log(LogLevel::WARNING, "Failed to parse HTML!");
        return {};
    }

    HTML& html = html_opt.value();
    Collection<Element> item_listings = html.SearchClass("WL_DZ",
        Element::BODY, true);

    ArenaProductList& results
        = *arena.allocate_object<ArenaProductList>(ArenaProductList::WithArena(arena));
    results.depth = depth;
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

        PMRProduct& pmr_product
            = *arena.allocate_object<PMRProduct>(ArenaProduct::WithArena(arena));

        ArenaProduct& product = std::get<ArenaProduct>(pmr_product);

        product.name = name_c[0].FirstChild().Text(true);
        product.image_url = image_c[0].GetAttrValue("src");
        product.url = std::format("{}/products/{}", stores::Tesco.homepage, id_text);
        product.id = std::format("{}{}", stores::Tesco.prefix, id_text);
        product.offers = {};
        product.item_price = price.value();
        product.store = stores::Tesco.id;
        product.price_per_unit = price_per.value();
        product.timestamp = std::time(nullptr);
        product.full_info = false;

        results.products.emplace_back(
            pmr_product,
            QueryResultInfo { results.products.size() }
        );

        if (results.products.size() >= depth) break;
    }

    return &results;
}

std::string TE_GetProductSearchURL(std::string_view query)
{
    char* buffer = curl_easy_escape(nullptr, query.data(), query.size());
    if (!buffer) {
        Log(LogLevel::WARNING, "Failed to escape query string {}", query);
        return {};
    }

    tb::scoped_guard free_buffer = [buffer] { curl_free(buffer); };
    return std::format("{}/search?query={}", stores::Tesco.homepage, buffer);
}

ArenaProduct* TE_GetProductAtURL(const HTML& html, tb::thread_safe_memory_arena& arena)
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

    ArenaProduct& result
        = *arena.allocate_object<ArenaProduct>(ArenaProduct::WithArena(arena));

    result.name = product_info["name"];
    result.description = product_info["description"];
    result.image_url = product_info["image"][0];
    result.url = std::format("{}/products/{}", stores::Tesco.homepage, sku);
    result.id = std::format("{}{}", stores::Tesco.prefix, sku);
    result.item_price = Price { Currency::EUR, price };
    result.store = stores::Tesco.id;
    result.timestamp = std::time(nullptr);
    result.full_info = true;

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

    return &result;
}

// Aldi

ArenaProductList* AL_ParseProductSearch(std::string_view data, tb::thread_safe_memory_arena& arena, size_t depth)
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
    auto& results
        = *arena.allocate_object<ArenaProductList>(ArenaProductList::WithArena(arena));
    results.depth = depth;
    results.products.reserve(items.size());
    size_t current_item = 0;
    for (json& item : items) {
        ++current_item;
        try {
            std::string_view brand_name
                = item["brandName"].is_string()
                ? item["brandName"].get<std::string_view>() : "";

            auto& pmr_product
                = *arena.allocate_object<PMRProduct>(ArenaProduct::WithArena(arena));

            auto& product = std::get<ArenaProduct>(pmr_product);

            product.name = std::format("{} {}", brand_name,
                                     item["name"].get<std::string_view>()),
            product.description = {};
            product.url = std::format("{}/product/{}", stores::Aldi.root_url,
                 item["sku"].get<std::string_view>());
            product.id = std::format("{}{}", stores::Aldi.prefix,
                 item["sku"].get<std::string_view>());
            product.item_price = Price {
                 Currency::EUR, item["price"]["amount"].get<unsigned>()
            };
            product.store = stores::Aldi.id;
            product.timestamp = std::time(nullptr);
            product.full_info = false;

            if (!item["assets"].empty()) {
                auto product_image_id = item["assets"][0]["url"].get<std::string_view>();
                constexpr size_t suffix_size = std::string_view("\\/{slug}").size();
                product_image_id.remove_suffix(suffix_size - 1);
                size_t image_id_start = product_image_id.rfind('/');
                product_image_id.remove_prefix(image_id_start + 1);

                product.image_url = std::format("{}/{}", BASE_IMAGE_URL,
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

            results.products.emplace_back(
                pmr_product,
                QueryResultInfo { results.products.size() }
            );

            if (results.products.size() >= depth)
                break;
        } catch (const json::exception& e) {
            Log(LogLevel::WARNING, "Invalid JSON for item #{} whilst parsing"
                " ALDI query: {}", current_item, e.what());
        }
    }

    return &results;
}

std::string AL_GetProductSearchURL(std::string_view query)
{
    char* buffer = curl_easy_escape(nullptr, query.data(), query.size());
    if (!buffer) {
        Log(LogLevel::WARNING, "Failed to escape query string {}", query);
        return {};
    }

    tb::scoped_guard free_buffer = [buffer] { curl_free(buffer); };

    return std::format(
        "https://api.aldi.ie/v3/product-search?&q={}&limit=30&sort=relevance",
        buffer
    );
}

ArenaProduct* AL_GetProductAtURL(const HTML& html, tb::thread_safe_memory_arena& arena)
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
