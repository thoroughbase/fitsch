#include "webscraper/database.hpp"

#include <exception>

#include "common/util.hpp"

using nlohmann::json;

Database::Database(const mongocxx::uri& uri) { Connect(uri); }

bool Database::Connect(const mongocxx::uri& uri)
{
    try {
        pool = std::make_unique<mongocxx::pool>(uri);
        if (Ping()) {
            Log(LogLevel::INFO, "Connected to database.");
            valid = true;
        }
    } catch (const std::exception& e) {
        Log(LogLevel::SEVERE,
            "Failed to establish connection to database: {}", e.what());
        valid = false;
    }

    return valid;
}

std::vector<Product> Database::GetProducts(std::span<const string> ids)
{
    return Get<Product>("products", "id", ids);
}

std::vector<QueryTemplate> Database::GetQueryTemplates(
    std::span<const string> search_terms)
{
    return Get<QueryTemplate>("queries", "query_string", search_terms);
}

void Database::PutProducts(std::span<const Product> products)
{
    Put<Product>("products", "id", products);
}

void Database::PutQueryTemplates(std::span<const QueryTemplate> query_templates)
{
    Put<QueryTemplate>("queries", "query_string", query_templates);
}

bool Database::Ping()
{
    try {
        json ping = { { "ping", 1 } };
        auto client = pool->acquire();
        auto admindb = (*client)["admin"];
        admindb.run_command(bsoncxx::from_json(ping.dump()));
    } catch (const std::exception& e) {
        Log(LogLevel::SEVERE, "Failed to ping database: {}", e.what());
        return false;
    }

    return true;
}
