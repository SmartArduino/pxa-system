/* Streaming backend, sharing the desktop backend's existing four request
 * slots. A condition variable provides backpressure without polling. */
#include "pxa/http_stream.h"

typedef struct {
    pxa_http_stream_t stream;
    pthread_cond_t changed;
    CURLM *multi;
    uint8_t upload_paused;
    uint8_t transfer_encoding;
    uint16_t status;
    uint64_t length;
} desktop_http_t;

static desktop_http_t *desktop_http(desktop_net_slot_t *slot) {
    return (desktop_http_t *)slot->body;
}
static void desktop_net_signal_locked(desktop_net_t *net) {
    net->wake_pending = 1;
    for (unsigned i = 0; i < DESKTOP_NET_MAX_PENDING; ++i) {
        desktop_net_slot_t *slot = net->slots[i];
        if (slot && slot->in_use && slot->streaming == 2)
            pthread_cond_broadcast(&desktop_http(slot)->changed);
    }
}

int pxsys_desktop_net_wait(pxa_net_backend_t *backend, uint32_t timeout_ms) {
    desktop_net_t *net = backend ? backend->context : NULL;
    desktop_net_slot_t *waiting = NULL;
    struct timespec until;
    if (!net || !timeout_ms) return 0;
    pthread_mutex_lock(&net->lock);
    if (!net->wake_pending) {
        for (unsigned i = 0; i < DESKTOP_NET_MAX_PENDING; ++i) {
            desktop_net_slot_t *slot = net->slots[i];
            if (slot && slot->in_use && slot->streaming == 2 &&
                !(pxa_http_stream_state(&desktop_http(slot)->stream) &
                  (PXA_HTTP_STREAM_DONE | PXA_HTTP_STREAM_CLOSED))) {
                waiting = slot;
                break;
            }
        }
        if (!waiting) {
            pthread_mutex_unlock(&net->lock);
            return 0;
        }
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += timeout_ms / 1000u;
        until.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
        if (until.tv_nsec >= 1000000000L) {
            ++until.tv_sec;
            until.tv_nsec -= 1000000000L;
        }
        /* Reuse a live exchange's existing condition. All network progress
         * broadcasts to every exchange, so another request cannot leave the
         * UI asleep. Only the owner calls wait/close/shutdown. */
        while (!net->wake_pending) {
            int result = pthread_cond_timedwait(&desktop_http(waiting)->changed,
                                                &net->lock, &until);
            if (result) break;
        }
    }
    net->wake_pending = 0;
    pthread_mutex_unlock(&net->lock);
    return 1;
}
static void desktop_http_notify(desktop_net_slot_t *slot) {
    desktop_net_notify(slot->net);
}
static int desktop_http_wait(desktop_net_slot_t *slot) {
    struct timespec until;
    uint64_t now = desktop_net_now_ms(), remaining;
    if (pxa_http_stream_state(&desktop_http(slot)->stream) & PXA_HTTP_STREAM_CLOSED)
        return 0;
    if (now >= slot->deadline_ms)
        return 0;
    remaining = slot->deadline_ms - now;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += (time_t)(remaining / 1000);
    until.tv_nsec += (long)(remaining % 1000) * 1000000L;
    if (until.tv_nsec >= 1000000000L) {
        ++until.tv_sec;
        until.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&desktop_http(slot)->changed, &slot->net->lock,
                                  &until) != ETIMEDOUT;
}
static size_t desktop_http_write(char *data, size_t size, size_t count, void *context) {
    desktop_net_slot_t *slot = context;
    pxa_http_stream_t *s = &desktop_http(slot)->stream;
    size_t bytes, total = 0;
    if (size && count > SIZE_MAX / size)
        return 0;
    bytes = size * count;
    if (bytes > s->max_body_bytes - s->received_bytes) {
        s->error = PXA_STATUS_LIMIT_EXCEEDED;
        return 0;
    }
    while (total < bytes) {
        uint8_t *destination;
        size_t available;
        pthread_mutex_lock(&slot->net->lock);
        while (!(available = pxa_http_ring_peek(&s->receive, &destination, 1))) {
            if (!desktop_http_wait(slot)) {
                pthread_mutex_unlock(&slot->net->lock);
                return 0;
            }
        }
        if (pxa_http_stream_state(s) & PXA_HTTP_STREAM_CLOSED) {
            pthread_mutex_unlock(&slot->net->lock);
            return 0;
        }
        if (available > bytes - total)
            available = bytes - total;
        memcpy(destination, data + total, available);
        pxa_http_ring_advance(&s->receive, available, 1);
        total += available;
        pthread_mutex_unlock(&slot->net->lock);
        desktop_http_notify(slot);
    }
    s->received_bytes += bytes;
    return bytes;
}
static size_t desktop_http_read(char *data, size_t size, size_t count, void *context) {
    desktop_net_slot_t *slot = context;
    pxa_http_stream_t *s = &desktop_http(slot)->stream;
    uint8_t *source;
    size_t available, capacity;
    if (size && count > SIZE_MAX / size)
        return CURL_READFUNC_ABORT;
    capacity = size * count;
    if (!capacity)
        return 0;
    pthread_mutex_lock(&slot->net->lock);
    for (;;) {
        if (pxa_http_stream_state(s) & PXA_HTTP_STREAM_CLOSED) {
            pthread_mutex_unlock(&slot->net->lock);
            return CURL_READFUNC_ABORT;
        }
        available = pxa_http_ring_peek(&s->send, &source, 0);
        if (available)
            break;
        if (pxa_http_stream_state(s) & PXA_HTTP_STREAM_UPLOAD_DONE) {
            pthread_mutex_unlock(&slot->net->lock);
            return 0;
        }
        desktop_http(slot)->upload_paused = 1;
        pthread_mutex_unlock(&slot->net->lock);
        return CURL_READFUNC_PAUSE;
    }
    if (available > capacity)
        available = capacity;
    memcpy(data, source, available);
    pxa_http_ring_advance(&s->send, available, 0);
    s->sent_bytes += available;
    pthread_mutex_unlock(&slot->net->lock);
    desktop_http_notify(slot);
    return available;
}
static size_t desktop_http_header(char *data, size_t size, size_t count,
                                  void *context) {
    desktop_net_slot_t *slot = context;
    desktop_http_t *h = desktop_http(slot);
    pxa_http_stream_t *s = &h->stream;
    char *colon, *value;
    size_t bytes, name_size, value_size;
    if (size && count > SIZE_MAX / size)
        return 0;
    bytes = size * count;
    if (bytes >= 5 && !memcmp(data, "HTTP/", 5)) {
        char *space = memchr(data, ' ', bytes);
        if (!space || data + bytes - space < 4 || space[1] < '0' || space[1] > '9' ||
            space[2] < '0' || space[2] > '9' || space[3] < '0' || space[3] > '9') {
            s->error = PXA_STATUS_PROTOCOL_ERROR;
            return 0;
        }
        h->status =
            (uint16_t)((space[1] - '0') * 100 + (space[2] - '0') * 10 + space[3] - '0');
        h->length = UINT64_MAX;
        h->transfer_encoding = 0;
        s->head_size = PXA_HTTP_HEAD_BYTES;
        return bytes;
    }
    if (bytes == 2 && !memcmp(data, "\r\n", 2)) {
        if (h->status >= 200) {
            uint64_t length = slot->method == PXA_NET_METHOD_HEAD || h->status == 204 ||
                                      h->status == 304
                                  ? 0
                                  : h->length;
            if (length != UINT64_MAX && length > s->max_body_bytes) {
                s->error = PXA_STATUS_LIMIT_EXCEEDED;
                return 0;
            }
            pxa_http_stream_headers(s, h->status, length);
            desktop_http_notify(slot);
        }
        return bytes;
    }
    /* Ignore trailers after the immutable response head was published. */
    if (pxa_http_stream_state(s) & PXA_HTTP_STREAM_HEADERS)
        return bytes;
    colon = memchr(data, ':', bytes);
    if (!colon)
        return bytes;
    name_size = (size_t)(colon - data);
    value = colon + 1;
    while (value < data + bytes && (*value == ' ' || *value == '\t'))
        ++value;
    value_size = (size_t)(data + bytes - value);
    while (value_size &&
           (value[value_size - 1] == '\r' || value[value_size - 1] == '\n'))
        --value_size;
    if (name_size == 14 && !strncasecmp(data, "content-length", 14)) {
        uint64_t length = 0;
        size_t i;
        if (!value_size) {
            s->error = PXA_STATUS_PROTOCOL_ERROR;
            return 0;
        }
        for (i = 0; i < value_size; ++i) {
            if (value[i] < '0' || value[i] > '9' ||
                length > (UINT64_MAX - (value[i] - '0')) / 10) {
                s->error = PXA_STATUS_PROTOCOL_ERROR;
                return 0;
            }
            length = length * 10 + (value[i] - '0');
        }
        if (h->length != UINT64_MAX && h->length != length) {
            s->error = PXA_STATUS_PROTOCOL_ERROR;
            return 0;
        }
        h->length = length;
    }
    if (name_size == 17 && !strncasecmp(data, "transfer-encoding", 17))
        h->transfer_encoding = 1;
    if (h->transfer_encoding && h->length != UINT64_MAX) {
        s->error = PXA_STATUS_PROTOCOL_ERROR;
        return 0;
    }
    if (slot->wanted_count && !desktop_net_name_matches(slot, data, name_size))
        return bytes;
    if (!pxa_http_stream_header(s, (const uint8_t *)data, name_size,
                                (const uint8_t *)value, value_size)) {
        s->error = PXA_STATUS_LIMIT_EXCEEDED;
        return 0;
    }
    return bytes;
}
static int desktop_http_progress(void *context, curl_off_t a, curl_off_t b,
                                 curl_off_t c, curl_off_t d) {
    desktop_net_slot_t *slot = context;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return (pxa_http_stream_state(&desktop_http(slot)->stream) &
            PXA_HTTP_STREAM_CLOSED) != 0;
}
static void *desktop_http_worker(void *context) {
    desktop_net_slot_t *slot = context;
    desktop_http_t *h = desktop_http(slot);
    pxa_http_stream_t *s = &h->stream;
    CURL *curl = curl_easy_init();
    CURLcode code = CURLE_OUT_OF_MEMORY;
    static const char *const methods[] = {"",    "GET",   "HEAD",  "POST",
                                          "PUT", "PATCH", "DELETE"};
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, slot->url);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)slot->timeout_ms);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
            (long)(slot->timeout_ms > 60000u ? 15000u : slot->timeout_ms));
        /* Redirects must return to the Guest for a new permission check. */
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slot->request_headers);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, desktop_http_header);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, slot);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, desktop_http_write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, slot);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, desktop_http_progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, slot);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        if (slot->method == PXA_NET_METHOD_HEAD)
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        if (s->upload_bytes || slot->method == PXA_NET_METHOD_POST ||
            slot->method == PXA_NET_METHOD_PUT ||
            slot->method == PXA_NET_METHOD_PATCH) {
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_READFUNCTION, desktop_http_read);
            curl_easy_setopt(curl, CURLOPT_READDATA, slot);
            curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                             (curl_off_t)s->upload_bytes);
        }
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, methods[slot->method]);
        CURLM *multi = curl_multi_init();
        int running = 1;
        if (multi) {
            pthread_mutex_lock(&slot->net->lock);
            h->multi = multi;
            pthread_mutex_unlock(&slot->net->lock);
            curl_multi_add_handle(multi, curl);
            while (running) {
                int resume = 0;
                pthread_mutex_lock(&slot->net->lock);
                if (h->upload_paused &&
                    (pxa_http_ring_readable(&s->send) ||
                     (pxa_http_stream_state(s) & PXA_HTTP_STREAM_UPLOAD_DONE))) {
                    h->upload_paused = 0;
                    resume = 1;
                }
                pthread_mutex_unlock(&slot->net->lock);
                if (pxa_http_stream_state(s) & PXA_HTTP_STREAM_CLOSED) {
                    code = CURLE_ABORTED_BY_CALLBACK;
                    break;
                }
                if (resume)
                    curl_easy_pause(curl, CURLPAUSE_CONT);
                if (curl_multi_perform(multi, &running) != CURLM_OK) {
                    code = CURLE_RECV_ERROR;
                    break;
                }
                if (running)
                    curl_multi_poll(multi, NULL, 0, 50, NULL);
            }
            CURLMsg *message;
            int remaining;
            while ((message = curl_multi_info_read(multi, &remaining)))
                if (message->msg == CURLMSG_DONE)
                    code = message->data.result;
            pthread_mutex_lock(&slot->net->lock);
            h->multi = NULL;
            pthread_mutex_unlock(&slot->net->lock);
            curl_multi_remove_handle(multi, curl);
            curl_multi_cleanup(multi);
        }
        curl_easy_cleanup(curl);
    }
    pthread_mutex_lock(&slot->net->lock);
    pxa_http_stream_done(s, s->error           ? s->error
                            : code == CURLE_OK ? PXA_STATUS_OK
                            : code == CURLE_OPERATION_TIMEDOUT ||
                                    desktop_net_now_ms() >= slot->deadline_ms
                                ? PXA_STATUS_TIMED_OUT
                                : PXA_STATUS_IO_ERROR);
    slot->complete = 1;
    desktop_net_t *net = slot->net;
    if (pxa_http_stream_state(s) & PXA_HTTP_STREAM_CLOSED) {
        pthread_cond_destroy(&h->changed);
        desktop_net_reset_slot(slot);
    }
    pthread_mutex_unlock(&net->lock);
    desktop_net_notify(net);
    desktop_net_worker_done(net);
    return NULL;
}
