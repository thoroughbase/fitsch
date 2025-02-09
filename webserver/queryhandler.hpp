#pragma once

#include <atomic>
#include <unordered_map>

#include <buxtehude/buxtehude.hpp>

#include <crow.h>

#include "common/product.hpp"

using QueryResultsMap = std::unordered_map<std::string, std::vector<Product>>;

struct RequestInfo
{
    std::promise<QueryResultsMap> promise;
    QueryResultsMap results;
    int expecting;
};

class QueryHandler
{
public:
    QueryHandler(buxtehude::Client& bclient, std::string_view webscraper);

    // Crow currently does not allow asynchronous request handling. For now, the
    // route lambdas block and wait on the future returned by this function.
    std::future<QueryResultsMap> SendQuery(std::string_view query);

private:
    std::unordered_map<int, RequestInfo> pending_queries;
    buxtehude::Client& bclient;
    std::string webscraper_name;

    std::atomic<int> request_id = 0;
};
