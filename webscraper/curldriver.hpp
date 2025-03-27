#pragma once

#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <thread>

#include <curl/curl.h>
#include <event2/event.h>

#include "common/util.hpp"

using TransferDoneCallback
    = std::function<void(std::string_view buffer, std::string_view url,
                         CURLcode result)>;

struct CURLHeaders
{
    curl_slist* header_list = nullptr;
    const std::vector<std::string> string_list;

    CURLHeaders() = default;
    CURLHeaders(std::span<std::string_view> headers);
    ~CURLHeaders();
};

inline const CURLHeaders CURLHEADERS_DEFAULT {};

inline const CURLHeaders CURLHEADERS_ALDI {
    tb::make_span<std::string_view>({
        "Accept: application/json, text/plain"
    })
};

struct CURLOptions
{
    enum class Method { GET, POST };

    std::string post_content;
    const CURLHeaders* headers = &CURLHEADERS_DEFAULT;
    Method method = Method::GET;
};

struct EasyHandleInfo
{
    std::string buffer;
    TransferDoneCallback callback = nullptr;
    event* add_transfer_event = nullptr;
    CURLM* multi_handle = nullptr;
    CURLOptions options;
    bool available = true;
};

struct TransferRequest
{
    std::string url;
    TransferDoneCallback callback;
    CURLOptions options;

    TransferRequest(std::string_view url, TransferDoneCallback&& cb,
        const CURLOptions& options)
    : url(url), callback(std::move(cb)), options(options) {}
};

struct GeneralCURLContext
{
    CURLM* multi_handle = nullptr;
    event_base* ebase = nullptr;
    event* timer_event = nullptr;

    CURLMcode return_code = CURLM_OK;
    int running_handles = 0;
    bool interrupt = false;
};

class CURLDriver
{
public:
    CURLDriver() = default;
    void Init(unsigned pool_size, std::string_view user_agent);
    ~CURLDriver();

    void PerformTransfer(std::string_view url, TransferDoneCallback&& callback,
        const CURLOptions& options = {});

    static bool GlobalInit(long flags = CURL_GLOBAL_DEFAULT);
    static void GlobalCleanup();
private:
    void PerformTransfer_NoLock(std::string_view url, TransferDoneCallback&& callback,
        const CURLOptions& options = {});
    void PerformNextInQueue();
    void Drive();

    std::unordered_map<CURL*, EasyHandleInfo> easy_handles;
    std::queue<TransferRequest> pending;
    std::mutex container_mutex;

    std::thread thread;

    GeneralCURLContext general_context;
};
