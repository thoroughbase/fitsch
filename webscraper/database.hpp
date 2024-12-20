#pragma once

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

using std::string;

// TODO: Consider other database architectures
class Database
{
public:
    Database();
    Database(const mongocxx::uri& uri);
    bool Connect(const mongocxx::uri& uri);
    ~Database();

    // Modify input by removing search terms that have been found
    template<typename T>
    std::vector<T> Get(const string& cl, std::string_view field,
                       std::vector<string>& terms)
    {
        if (!valid) {
            Log(WARNING, "Not connected to database, could not get documents");
            return {};
        }
        std::vector<T> matches;

        auto client = pool->acquire();
        mongocxx::database db = (*client)["fitsch"];
        auto collection = db.collection(cl);

        json search_term = {
            { field, {{ "$in", terms }} }
        };

        try {
            auto results = collection.find(bsoncxx::from_json(search_term.dump()));

            for (auto& r : results) {
                json j = json::parse(bsoncxx::to_json(r));
                T t = j.get<T>();
                for (auto it = terms.begin(); it != terms.end(); ++it) {
                    if (*it == std::string(j[field])) { terms.erase(it); break; }
                }
                matches.push_back(t);
            }

        } catch (const std::exception& e) {
            Log(WARNING, "Error searching with term `{}`: {}", search_term.dump(),
                e.what());
            return matches;
        }
        return matches;
    }

    template<typename T>
    void Put(const string& cl, std::string_view field, const std::vector<T>& items)
    {
        if (!valid) {
            Log(WARNING, "Not connected to database, could not put documents");
            return;
        }

        if (items.empty()) return;

        auto client = pool->acquire();
        mongocxx::database db = (*client)["fitsch"];
        auto collection = db.collection(cl);

        std::vector<bsoncxx::document::value> docs;
        std::vector<string> removals;
        for (auto& i : items) {
            json j = i;

            docs.push_back(bsoncxx::from_json(j.dump()));
            removals.push_back(j[field]);
        }

        json remove_command = {
            { field, {{ "$in", removals }} }
        };

        try {
            collection.delete_many(bsoncxx::from_json(remove_command.dump()));
            collection.insert_many(docs);
        } catch (const std::exception& e) {
            Log(WARNING, "Error putting documents: {}", e.what());
        }
    }

    std::vector<Product> GetProducts(std::vector<string>& id);
    std::vector<QueryTemplate> GetQueryTemplates(std::vector<string>& search_terms);

    void PutProducts(const std::vector<Product>& product);
    void PutQueryTemplates(const std::vector<QueryTemplate>& q);

    bool Ping();

private:
    mongocxx::pool* pool;
    mongocxx::instance instance;
    bool valid = false;
};
