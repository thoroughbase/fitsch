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
            Log(INFO, "Connected to database.");
            valid = true;
        }
    } catch (const std::exception& e) {
        Log(SEVERE, "Failed to establish connection to database: {}", e.what());
        valid = false;
    }

    return valid;
}

std::vector<Product> Database::GetProducts(std::vector<string>& ids)
{
    return Get<Product>("products", "id", ids);
}

std::vector<QueryTemplate> Database::GetQueryTemplates(std::vector<string>& str)
{
    return Get<QueryTemplate>("queries", "query_string", str);
}

void Database::PutProducts(const std::vector<Product>& product)
{
    Put<Product>("products", "id", product);
}

void Database::PutQueryTemplates(const std::vector<QueryTemplate>& q)
{
    Put<QueryTemplate>("queries", "query_string", q);
}

bool Database::Ping()
{
    try {
        json ping = { { "ping", 1 } };
        auto client = pool->acquire();
        auto admindb = (*client)["admin"];
        admindb.run_command(bsoncxx::from_json(ping.dump()));
    } catch (const std::exception& e) {
        Log(SEVERE, "Failed to ping database: {}", e.what());
        return false;
    }

    return true;
}
