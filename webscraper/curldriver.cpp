#include "webscraper/curldriver.hpp"

#include <event2/thread.h>
#include <sys/time.h>

#include "common/util.hpp"

// Libevent callbacks

static void Libevent_TimerCallback(int fd, short what, void* general_ctx)
{
    auto* ctx = static_cast<GeneralCURLContext*>(general_ctx);

    ctx->return_code = curl_multi_socket_action(ctx->multi_handle, CURL_SOCKET_TIMEOUT,
        0, &ctx->running_handles);
    event_base_loopbreak(ctx->ebase);
}

static void Libevent_ReadWriteCallback(int fd, short what, void* general_ctx)
{
    auto* ctx = static_cast<GeneralCURLContext*>(general_ctx);

    int curl_what = 0;
    if (what & EV_READ) curl_what |= CURL_CSELECT_IN;
    if (what & EV_WRITE) curl_what |= CURL_CSELECT_OUT;

    ctx->return_code = curl_multi_socket_action(ctx->multi_handle, fd, curl_what,
        &ctx->running_handles);
    event_base_loopbreak(ctx->ebase);
}

static void Libevent_InterruptCallback(int fd, short what, void* general_ctx)
{
    auto* ctx = static_cast<GeneralCURLContext*>(general_ctx);

    ctx->interrupt = true;
    event_base_loopbreak(ctx->ebase);
}

static void Libevent_AddTransferCallback(int fd, short what, void* handle_pair)
{
    auto* ctx = static_cast<std::pair<CURL*, EasyHandleInfo>*>(handle_pair);
    auto& [easy_handle_to_add, handle_info] = *ctx;

    curl_multi_add_handle(handle_info.multi_handle, easy_handle_to_add);
    int dummy;
    curl_multi_socket_action(handle_info.multi_handle, CURL_SOCKET_TIMEOUT, 0, &dummy);
}

// CURL callbacks

static size_t CURL_WriteData(char* data, size_t size, size_t nmemb, std::string* buffer)
{
    buffer->append(data, size * nmemb);

    return size * nmemb;
}

static int CURL_SocketInfoCallback(CURL* easy_handle, int fd, int what, void* general_ctx,
    void* socket_ctx)
{
    auto* g_ctx = static_cast<GeneralCURLContext*>(general_ctx);
    auto* s_ctx = static_cast<SocketCURLContext*>(socket_ctx);

    auto& sock_contexts = g_ctx->socket_contexts;
    if (!s_ctx) {
        auto& ref = sock_contexts.emplace_back(std::make_unique<SocketCURLContext>());

        s_ctx = ref.get();
        s_ctx->fd = fd;
        curl_multi_assign(g_ctx->multi_handle, fd, s_ctx);
    }

    if (what & CURL_POLL_REMOVE) {
        std::erase_if(sock_contexts, [fd] (auto& unique_ptr) {
            return unique_ptr->fd == fd;
        });

        curl_multi_assign(g_ctx->multi_handle, fd, nullptr);
    } else if (what & CURL_POLL_INOUT) {
        if (s_ctx->read_write_event) event_free(s_ctx->read_write_event);

        int ev_flags = EV_PERSIST;
        if (what & CURL_POLL_IN) ev_flags |= EV_READ;
        if (what & CURL_POLL_OUT) ev_flags |= EV_WRITE;

        s_ctx->read_write_event = event_new(g_ctx->ebase, fd, ev_flags,
            Libevent_ReadWriteCallback, g_ctx);

        event_add(s_ctx->read_write_event, nullptr);
    }

    return 0;
}

static int CURL_TimerInfoCallback(CURLM* multi_handle, long timeout, void* general_ctx)
{
    auto* ctx = static_cast<GeneralCURLContext*>(general_ctx);

    if (ctx->timer_event) {
        event_free(ctx->timer_event);
        ctx->timer_event = nullptr;
    }

    if (timeout < 0) return 0;

    timeval timer_val = {
        static_cast<int>(timeout / 1000),
        static_cast<int>(timeout % 1000) * 1000
    };
    ctx->timer_event = evtimer_new(ctx->ebase, Libevent_TimerCallback, general_ctx);
    evtimer_add(ctx->timer_event, &timer_val);

    return 0;
}

// Struct functions

SocketCURLContext::~SocketCURLContext()
{
    if (read_write_event) event_free(read_write_event);
}

CURLHeaders::~CURLHeaders()
{
    if (header_list) curl_slist_free_all(header_list);
}

CURLHeaders::CURLHeaders(std::span<std::string_view> headers)
: string_list(headers.begin(), headers.end())
{
    for (const std::string& header : string_list) {
        curl_slist* success = curl_slist_append(header_list, header.c_str());
        if (!success) {
            Log(LogLevel::SEVERE, "Failed to create header list!");
            return;
        }
        header_list = success;
    }
}

// CURLDriver

CURLDriver::CURLDriver(int pool_size, std::string_view user_agent)
{
    general_context.ebase = event_base_new();
    general_context.multi_handle = curl_multi_init();
    CURLM* multi_handle = general_context.multi_handle;

    curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, CURL_SocketInfoCallback);
    curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, CURL_TimerInfoCallback);

    curl_multi_setopt(multi_handle, CURLMOPT_SOCKETDATA, &general_context);
    curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, &general_context);

    for (int i = 0; i < pool_size; ++i) {
        easy_handles.emplace(curl_easy_init(), EasyHandleInfo {
            .multi_handle = multi_handle
        });
    }

    // Separate for loop to avoid iterator invalidation from potential rehashing
    for (auto& [easy_handle, info] : easy_handles) {
        info.add_transfer_event = event_new(general_context.ebase, -1, 0,
            Libevent_AddTransferCallback, &(*easy_handles.find(easy_handle)));

        curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, CURL_WriteData);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, &info.buffer);
        curl_easy_setopt(easy_handle, CURLOPT_USERAGENT, user_agent.data());
    }
}

CURLDriver::~CURLDriver()
{
    running = false;

    event* interrupt_event = event_new(general_context.ebase, -1, 0,
        Libevent_InterruptCallback, &general_context);
    event_active(interrupt_event, 0, 0);

    if (thread.joinable()) thread.join();

    event_free(interrupt_event);

    for (auto& [handle, info] : easy_handles) {
        curl_multi_remove_handle(general_context.multi_handle, handle);
        event_free(info.add_transfer_event);
        curl_easy_cleanup(handle);
    }

    if (general_context.timer_event) event_free(general_context.timer_event);

    curl_multi_cleanup(general_context.multi_handle);
    event_base_free(general_context.ebase);
}

void CURLDriver::Run()
{
    if (running) return;
    running = true;

    std::thread thr(&CURLDriver::Drive, this);
    thread = std::move(thr);
}

void CURLDriver::PerformTransfer(std::string_view url, TransferDoneCallback&& cb,
    const CURLOptions& options)
{
    std::lock_guard<std::mutex> guard(container_mutex);

    PerformTransfer_NoLock(url, std::forward<TransferDoneCallback>(cb), options);
}

bool CURLDriver::GlobalInit(long flags)
{
    evthread_use_pthreads();
    return !curl_global_init(flags);
}

void CURLDriver::GlobalCleanup()
{
    curl_global_cleanup();
}

void CURLDriver::PerformTransfer_NoLock(std::string_view url, TransferDoneCallback&& cb,
    const CURLOptions& options)
{
    CURL* easy_handle = nullptr;
    EasyHandleInfo* handle_info = nullptr;

    for (auto& [handle, info] : easy_handles) {
        if (!info.available) continue;
        easy_handle = handle;
        handle_info = &info;
        break;
    }

    if (!easy_handle) {
        pending.emplace(std::string(url), std::forward<TransferDoneCallback>(cb),
            options);
        return;
    }

    switch (options.method) {
    case CURLOptions::Method::GET:
        curl_easy_setopt(easy_handle, CURLOPT_HTTPGET, 1);
        break;
    case CURLOptions::Method::POST:
        curl_easy_setopt(easy_handle, CURLOPT_POST, 1);
        curl_easy_setopt(easy_handle, CURLOPT_COPYPOSTFIELDS,
            options.post_content.empty() ? "" : options.post_content.c_str());
        break;
    }

    curl_easy_setopt(easy_handle, CURLOPT_URL, url.data());
    if (options.headers->header_list)
        curl_easy_setopt(easy_handle, CURLOPT_HTTPHEADER, options.headers->header_list);

    handle_info->callback = std::move(cb);
    handle_info->available = false;
    handle_info->buffer.clear();
    handle_info->options = options;

    // Initiating transfers via a libevent callback ensures that CURL callbacks
    // are only ever triggered from one thread for thread-safety with no locks
    event_active(handle_info->add_transfer_event, 0, 0);
}

void CURLDriver::PerformNextInQueue()
{
    if (pending.empty()) return;

    TransferRequest& request = pending.front();
    PerformTransfer_NoLock(request.url,
        std::forward<TransferDoneCallback>(request.callback));
    pending.pop();
}

void CURLDriver::Drive()
{
    while (event_base_loop(general_context.ebase, EVLOOP_NO_EXIT_ON_EMPTY) == 0) {
        if (general_context.interrupt) break;

        std::lock_guard<std::mutex> guard(container_mutex);
        if (CURLMcode error_code = general_context.return_code;
            error_code != CURLM_OK) {

            Log(LogLevel::WARNING,
                "Error occurred during curl transfer: {} (CURLMcode = {})",
                curl_multi_strerror(error_code), static_cast<int>(error_code));
            Log(LogLevel::INFO, "Re-registering handles & events...");

            event_free(general_context.timer_event);
            general_context.timer_event = nullptr;

            general_context.socket_contexts.clear();

            for (auto& [handle, info] : easy_handles) {
                if (info.available) continue;
                info.buffer.clear();
                // Not sure if this fixes things...
                // Official libcurl documentation states that all handles should be
                // removed and "new ones should be added" in the event of an error
                // from curl_multi_socket_action
                curl_multi_remove_handle(general_context.multi_handle, handle);
                curl_multi_add_handle(general_context.multi_handle, handle);
            }
            continue;
        }

        CURLM* multi_handle = general_context.multi_handle;

        int messages;
        CURLMsg* message;
        while ((message = curl_multi_info_read(multi_handle, &messages))
            != nullptr) {

            if (message->msg != CURLMSG_DONE) continue;

            CURLcode error_code = message->data.result;
            if (error_code != CURLE_OK) {
                Log(LogLevel::WARNING,
                    "Error occurred during curl transfer: {} (CURLcode = {})",
                    curl_easy_strerror(error_code), static_cast<int>(error_code));
            }

            EasyHandleInfo& info = easy_handles[message->easy_handle];

            char* url = nullptr;
            curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL, &url);

            if (info.callback)
                info.callback(info.buffer, url, error_code);

            curl_multi_remove_handle(multi_handle, message->easy_handle);
            info.available = true;

            PerformNextInQueue();
        }
    }
}
