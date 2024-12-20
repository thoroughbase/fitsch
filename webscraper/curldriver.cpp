#include "webscraper/curldriver.hpp"

#include <event2/thread.h>
#include <sys/time.h>

// Libevent callbacks

static void Libevent_TimerCallback(int fd, short what, void* general_ctx)
{
    GeneralCURLContext* ctx = (GeneralCURLContext*) general_ctx;

    ctx->return_code = curl_multi_socket_action(ctx->multi_handle, CURL_SOCKET_TIMEOUT,
        0, &ctx->running_handles);
    event_base_loopbreak(ctx->ebase);
}

static void Libevent_ReadWriteCallback(int fd, short what, void* general_ctx)
{
    GeneralCURLContext* ctx = (GeneralCURLContext*) general_ctx;

    int curl_what = 0;
    if (what & EV_READ) curl_what |= CURL_CSELECT_IN;
    if (what & EV_WRITE) curl_what |= CURL_CSELECT_OUT;

    ctx->return_code = curl_multi_socket_action(ctx->multi_handle, fd, curl_what,
        &ctx->running_handles);
    event_base_loopbreak(ctx->ebase);
}

static void Libevent_InterruptCallback(int fd, short what, void* general_ctx)
{
    GeneralCURLContext* ctx = (GeneralCURLContext*) general_ctx;

    ctx->interrupt = true;
    event_base_loopbreak(ctx->ebase);
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
    GeneralCURLContext* g_ctx = (GeneralCURLContext*) general_ctx;
    SocketCURLContext* s_ctx = (SocketCURLContext*) socket_ctx;

    auto& sock_contexts = g_ctx->socket_contexts;
    if (!s_ctx) {
        auto& ref = sock_contexts.emplace_back(std::make_unique<SocketCURLContext>());

        s_ctx = ref.get();
        s_ctx->fd = fd;
        curl_multi_assign(g_ctx->multi_handle, fd, s_ctx);
    }

    if (what & CURL_POLL_REMOVE) {
        if (s_ctx->read_write_event) event_free(s_ctx->read_write_event);

        auto iterator = std::find_if(sock_contexts.begin(), sock_contexts.end(),
            [fd] (auto& unique_ptr) {
                return unique_ptr->fd == fd;
            }
        );

        if (iterator != sock_contexts.end()) sock_contexts.erase(iterator);

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
    GeneralCURLContext* ctx = (GeneralCURLContext*) general_ctx;

    if (ctx->timer_event) {
        event_free(ctx->timer_event);
        ctx->timer_event = nullptr;
    }

    if (timeout < 0) return 0;

    timeval timer_val = { (int)timeout / 1000, ((int)timeout % 1000) * 1000 };
    ctx->timer_event = evtimer_new(ctx->ebase, Libevent_TimerCallback, general_ctx);
    evtimer_add(ctx->timer_event, &timer_val);

    return 0;
}

// CURLDriver

CURLDriver::CURLDriver(int pool_size)
{
    general_context.ebase = event_base_new();
    general_context.multi_handle = curl_multi_init();
    CURLM* multi_handle = general_context.multi_handle;

    curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, CURL_SocketInfoCallback);
    curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, CURL_TimerInfoCallback);

    curl_multi_setopt(multi_handle, CURLMOPT_SOCKETDATA, &general_context);
    curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, &general_context);

    for (int i = 0; i < pool_size; ++i) {
        easy_handles.emplace(curl_easy_init(), EasyHandleInfo {});
    }

    // Separate for loop to avoid iterator invalidation from potential rehashing
    for (auto& [easy_handle, info] : easy_handles) {
        curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, CURL_WriteData);
        curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, &info.buffer);
    }
}

CURLDriver::~CURLDriver()
{
    running = false;

    event* interrupt_event = event_new(general_context.ebase, -1, 0,
        Libevent_InterruptCallback, &general_context);
    event_active(interrupt_event, 0, 0);

    if (thread.joinable()) thread.join();

    for (auto& [handle, info] : easy_handles) {
        curl_multi_remove_handle(general_context.multi_handle, handle);
        curl_easy_cleanup(handle);
    }

    if (general_context.timer_event) event_free(general_context.timer_event);

    for (auto& unique_ptr : general_context.socket_contexts) {
        if (unique_ptr->read_write_event) event_free(unique_ptr->read_write_event);
    }

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

void CURLDriver::PerformTransfer(std::string_view url, TransferDoneCallback cb)
{
    std::lock_guard<std::mutex> guard(container_mutex);

    PerformTransfer_NoLock(url, cb);
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

void CURLDriver::PerformTransfer_NoLock(std::string_view url, TransferDoneCallback cb)
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
        // Add to queue
        pending.push(TransferRequest { std::string(url), cb });
        return;
    }

    curl_easy_setopt(easy_handle, CURLOPT_URL, url.data());

    handle_info->callback = cb;
    handle_info->available = false;
    handle_info->buffer.clear();

    curl_multi_add_handle(general_context.multi_handle, easy_handle);

    general_context.return_code =
        curl_multi_socket_action(general_context.multi_handle,
            CURL_SOCKET_TIMEOUT, 0, &general_context.running_handles);
}

void CURLDriver::PerformNextInQueue()
{
    if (pending.empty()) return;

    TransferRequest request = pending.front();
    pending.pop();

    PerformTransfer_NoLock(request.url, request.callback);
}

void CURLDriver::Drive()
{
    while (event_base_loop(general_context.ebase, EVLOOP_NO_EXIT_ON_EMPTY) == 0) {
        if (general_context.interrupt) break;

        if (general_context.return_code) {
            // Error, remove handles
        }

        CURLM* multi_handle = general_context.multi_handle;

        int messages;
        CURLMsg* message;
        while ((message = curl_multi_info_read(multi_handle, &messages))
            != nullptr) {
            if (message->msg != CURLMSG_DONE) continue;

            std::lock_guard<std::mutex> guard(container_mutex);

            EasyHandleInfo& info = easy_handles[message->easy_handle];
            if (info.callback) {
                char* url = nullptr;
                curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL, &url);

                info.callback(info.buffer, url, message->data.result);
            }
            curl_multi_remove_handle(multi_handle, message->easy_handle);
            info.available = true;

            PerformNextInQueue();
        }
    }
}
