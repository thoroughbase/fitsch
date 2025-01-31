#pragma once

#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <thread>

#include <curl/curl.h>
#include <event2/event.h>

using TransferDoneCallback
    = std::function<void(std::string_view buffer, std::string_view url,
                         CURLcode result)>;

struct EasyHandleInfo
{
    std::string buffer;
    TransferDoneCallback callback = nullptr;
    event* add_transfer_event = nullptr;
    CURLM* multi_handle = nullptr;
    bool available = true;
};

struct TransferRequest
{
    std::string url;
    TransferDoneCallback callback;

    TransferRequest(std::string_view url, TransferDoneCallback&& cb)
    : url(url), callback(std::move(cb)) {}
};

struct SocketCURLContext
{
    event* read_write_event = nullptr;
    int fd;

    ~SocketCURLContext();
};

struct GeneralCURLContext
{
    std::vector<std::unique_ptr<SocketCURLContext>> socket_contexts;

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
    CURLDriver(int pool_size, std::string_view user_agent);
    ~CURLDriver();

    // Creates a new thread for libevent & curl feedback loops
    void Run();

    void PerformTransfer(std::string_view url, TransferDoneCallback&& callback);

    static bool GlobalInit(long flags = CURL_GLOBAL_DEFAULT);
    static void GlobalCleanup();
private:
    void PerformTransfer_NoLock(std::string_view url, TransferDoneCallback&& callback);
    void PerformNextInQueue();
    void Drive();

    std::unordered_map<CURL*, EasyHandleInfo> easy_handles;
    std::queue<TransferRequest> pending;
    std::mutex container_mutex;

    std::thread thread;

    GeneralCURLContext general_context;

    std::atomic<bool> running = false;
};
