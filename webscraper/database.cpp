#include "webscraper/database.hpp"

#include <exception>

#include "common/util.hpp"

using nlohmann::json;

Database::Database(std::string_view uri) : pool(mongocxx::uri { std::string(uri) })
{
    if ((valid = Ping())) {
        Log(LogLevel::INFO, "Connected to database.");
    } else {
        Log(LogLevel::SEVERE, "Failed to establish connection to database");
    }
}

std::vector<Product> Database::GetProducts(std::span<const std::string> ids)
{
    return Get<Product>("products", "id", ids);
}

std::vector<QueryTemplate> Database::GetQueryTemplates(
    std::span<const std::string> search_terms)
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
        mongocxx::pool::entry client = pool.acquire();
        mongocxx::database admindb = (*client)["admin"];
        admindb.run_command(bsoncxx::from_json(ping.dump()));
    } catch (const mongocxx::exception& e) {
        Log(LogLevel::SEVERE, "Failed to ping database: {}", e.what());
        return false;
    }

    return true;
}
