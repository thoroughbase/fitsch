#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <mongocxx/instance.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/json.hpp>

#include <nlohmann/json.hpp>

#include "common/product.hpp"

// TODO: Consider other database architectures
class Database
{
public:
    Database() = default;
    Database(const mongocxx::uri& uri);
    bool Connect(const mongocxx::uri& uri);

    template<typename T>
    std::vector<T> Get(const std::string& collection_name, std::string_view field,
                       std::span<const std::string> terms)
    {
        if (!valid) {
            Log(LogLevel::WARNING,
                "Not connected to database, could not get documents");
            return {};
        }

        auto client = pool->acquire();
        mongocxx::database db = (*client)["fitsch"];
        auto collection = db.collection(collection_name);

        json search_term = {
            { field, {{ "$in", terms }} }
        };

        std::vector<T> matches;
        matches.reserve(terms.size());
        try {
            auto results = collection.find(bsoncxx::from_json(search_term.dump()));

            for (auto& r : results) {
                json j = json::parse(bsoncxx::to_json(r));
                matches.emplace_back(j.get<T>());
            }
        } catch (const std::exception& e) {
            Log(LogLevel::WARNING, "Error searching with term `{}`: {}",
                search_term.dump(), e.what());
        }
        return matches;
    }

    template<typename T>
    void Put(const std::string& collection_name, std::string_view field,
             std::span<const T> items)
    {
        if (!valid) {
            Log(LogLevel::WARNING,
                "Not connected to database, could not put documents");
            return;
        }

        if (items.empty()) return;

        auto client = pool->acquire();
        mongocxx::database db = (*client)["fitsch"];
        auto collection = db.collection(collection_name);

        auto removals =
            items | std::views::transform([field] (const json& item) {
                return item[field].get<std::string>();
            }) | tb::range_to<std::vector<std::string>>();

        auto docs =
            items | std::views::transform([] (const json& item) {
                return bsoncxx::from_json(item.dump());
            }) | tb::range_to<std::vector<bsoncxx::document::value>>();

        json remove_command = {
            { field, {{ "$in", removals }} }
        };

        try {
            collection.delete_many(bsoncxx::from_json(remove_command.dump()));
            collection.insert_many(docs);
        } catch (const std::exception& e) {
            Log(LogLevel::WARNING, "Error putting documents: {}", e.what());
        }
    }

    std::vector<Product> GetProducts(std::span<const std::string> id);
    std::vector<QueryTemplate> GetQueryTemplates(
        std::span<const std::string> search_terms);

    void PutProducts(std::span<const Product> products);
    void PutQueryTemplates(std::span<const QueryTemplate> query_templates);

    bool Ping();

private:
    std::unique_ptr<mongocxx::pool> pool;
    mongocxx::instance instance;
    bool valid = false;
};
