#pragma once

#include <string>
#include <cstdlib>
#include <ctime>
#include <algorithm>

#include <curl/curl.h>
#include <fmt/format.h>
#include <fmt/chrono.h>

#define MIN_LOG_LEVEL DEBUG

enum LogLevel { DEBUG = 0, INFO = 1, WARNING = 2, SEVERE = 3 };

using std::string;

template <typename... T>
void Log(LogLevel l, fmt::format_string<T...> format, T&&... args)
{
    const static char* LEVEL_NAMES[] = { "DEBUG", "INFO", "WARNING", "SEVERE" };

    if (l < MIN_LOG_LEVEL) return;

    fmt::print("[{} {:%H:%M:%S}] {}\n", LEVEL_NAMES[l],
               fmt::localtime(std::time(nullptr)),
               fmt::vformat(format, fmt::make_format_args(args...)));
}

string RetrievePage(CURL* curl, const string& url);

inline const string BLANK;
