#include "common/util.hpp"

#include <fstream>
#include <iostream>

#include <curl/curl.h>

#include <utility>

size_t CURLWriteData(char* buf, size_t size, size_t nmemb, std::string* str)
{
    str->append(buf, size * nmemb);

    return size * nmemb;
}

string RetrievePage(CURL* curl, const string& url)
{
    string result;

    char* error = (char*) malloc(CURL_ERROR_SIZE);
    memset(error, 0, CURL_ERROR_SIZE);

    curl_easy_setopt(curl, CURLOPT_URL, url.data());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CURLWriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);

    CURLcode errcode;

    if ((errcode = curl_easy_perform(curl))) {
        Log(WARNING, "Error whilst retrieving page at {}: [{}] {}", url,
            (int)errcode, error);
        result.clear();
    }

    free(error);

 	return result;
}
