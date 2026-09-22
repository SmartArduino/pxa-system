/* Desktop POSIX/libcurl network backend for the PXA net service.
 *
 * The ESP product uses esp_http_client on a dedicated worker; the desktop
 * simulator mirrors the same asynchronous contract with one detached worker
 * thread per request. curl owns HTTP framing, redirects and TLS, while the
 * service keeps validation, permissions and body streaming. */

#include "desktop_net.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>

#define DESKTOP_NET_MAX_PENDING UINT16_C(4)
#define DESKTOP_NET_MAX_RESPONSE_BYTES UINT32_C(262144)
#define DESKTOP_NET_MAX_INLINE_BODY_BYTES UINT32_C(65536)
#define DESKTOP_NET_MAX_HEADER_BLOCK_BYTES UINT32_C(2048)
#define DESKTOP_NET_MAX_URL_BYTES UINT32_C(1024)
#define DESKTOP_NET_MAX_REDIRECTS 3L
#define DESKTOP_NET_WANTED_NAME_BYTES PXA_NET_MAX_HEADER_NAME_BYTES

typedef struct desktop_net_slot desktop_net_slot_t;

typedef struct {
    pthread_mutex_t lock;
    uint64_t next_operation;
    pxsys_desktop_net_notify_fn notify;
    void *notify_context;
    desktop_net_slot_t *slots[DESKTOP_NET_MAX_PENDING];
} desktop_net_t;

struct desktop_net_slot {
    desktop_net_t *net;
    uint64_t operation;
    uint8_t in_use;
    uint8_t complete;
    uint8_t cancelled;
    uint8_t streaming;
    pxa_status_t result;
    /* Request copy. */
    char url[DESKTOP_NET_MAX_URL_BYTES];
    uint16_t method;
    uint16_t timeout_ms;
    uint32_t max_response_bytes;
    struct curl_slist *request_headers;
    uint8_t *request_body;
    size_t request_body_size;
    char wanted_names[PXA_NET_MAX_HEADERS][DESKTOP_NET_WANTED_NAME_BYTES];
    size_t wanted_count;
    uint64_t deadline_ms;
    /* Response. */
    uint16_t status_code;
    char content_type[PXA_NET_MAX_HEADER_VALUE_BYTES];
    uint8_t *body;
    size_t body_size;
    size_t body_offset;
    pxa_net_header_t response_headers[PXA_NET_MAX_HEADERS];
    char header_storage[DESKTOP_NET_MAX_HEADER_BLOCK_BYTES];
    size_t header_storage_size;
    size_t response_header_count;
    uint8_t header_overflow;
    uint8_t response_too_large;
};

static desktop_net_t *g_desktop_net;
static pthread_once_t g_curl_once = PTHREAD_ONCE_INIT;

static void desktop_net_curl_init(void) {
    (void)curl_global_init(CURL_GLOBAL_DEFAULT);
}

static uint64_t desktop_net_now_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static int desktop_net_name_matches(const desktop_net_slot_t *slot,
                                    const char *name, size_t name_size) {
    size_t index;
    for (index = 0; index < slot->wanted_count; ++index) {
        if (strlen(slot->wanted_names[index]) == name_size &&
            strncasecmp(slot->wanted_names[index], name, name_size) == 0)
            return 1;
    }
    return 0;
}

static size_t desktop_net_write_cb(char *data, size_t size, size_t count,
                                   void *user_data) {
    desktop_net_slot_t *slot = (desktop_net_slot_t *)user_data;
    const size_t bytes = size * count;
    uint8_t *replacement;
    if (slot == NULL || bytes == 0) return 0;
    if (slot->body_size > slot->max_response_bytes ||
        bytes > slot->max_response_bytes - slot->body_size) {
        slot->response_too_large = 1;
        return 0;
    }
    replacement = realloc(slot->body, slot->body_size + bytes);
    if (replacement == NULL) {
        slot->response_too_large = 1;
        return 0;
    }
    memcpy(replacement + slot->body_size, data, bytes);
    slot->body = replacement;
    slot->body_size += bytes;
    return bytes;
}

static size_t desktop_net_header_cb(char *data, size_t size, size_t count,
                                    void *user_data) {
    desktop_net_slot_t *slot = (desktop_net_slot_t *)user_data;
    const size_t bytes = size * count;
    char *colon;
    char *value;
    size_t value_size;
    if (slot == NULL || data == NULL) return 0;
    if (bytes >= 5u && strncmp(data, "HTTP/", 5u) == 0) {
        /* A new response starts (redirect or 100-continue): drop the previous
         * status and header set so only the final response is reported. */
        slot->status_code = 0;
        slot->content_type[0] = '\0';
        slot->response_header_count = 0;
        slot->header_storage_size = 0;
        return bytes;
    }
    colon = memchr(data, ':', bytes);
    if (colon == NULL) return bytes;
    value = colon + 1;
    while (value < data + bytes && (*value == ' ' || *value == '\t')) ++value;
    value_size = (size_t)(data + bytes - value);
    while (value_size != 0 &&
           (value[value_size - 1] == '\r' || value[value_size - 1] == '\n'))
        --value_size;
    if ((size_t)(colon - data) == 10u &&
        strncasecmp(data, "Content-Type", 12u) == 0) {
        if (value_size >= sizeof(slot->content_type))
            value_size = sizeof(slot->content_type) - 1u;
        memcpy(slot->content_type, value, value_size);
        slot->content_type[value_size] = '\0';
        return bytes;
    }
    if (slot->response_header_count < PXA_NET_MAX_HEADERS &&
        desktop_net_name_matches(slot, data, (size_t)(colon - data))) {
        pxa_net_header_t *header =
            &slot->response_headers[slot->response_header_count];
        const size_t name_size = (size_t)(colon - data);
        if (slot->header_storage_size + name_size + 1u + value_size >
            sizeof(slot->header_storage)) {
            slot->header_overflow = 1;
            return bytes;
        }
        memcpy(slot->header_storage + slot->header_storage_size, data,
               name_size);
        header->name.data =
            (const uint8_t *)(slot->header_storage + slot->header_storage_size);
        header->name.size = name_size;
        slot->header_storage_size += name_size + 1u;
        slot->header_storage[slot->header_storage_size - 1u] = '\0';
        memcpy(slot->header_storage + slot->header_storage_size, value,
               value_size);
        header->value.data =
            (const uint8_t *)(slot->header_storage + slot->header_storage_size);
        header->value.size = value_size;
        slot->header_storage_size += value_size + 1u;
        slot->header_storage[slot->header_storage_size - 1u] = '\0';
        ++slot->response_header_count;
    }
    return bytes;
}

static int desktop_net_progress_cb(void *user_data, curl_off_t download_total,
                                   curl_off_t download_now,
                                   curl_off_t upload_total,
                                   curl_off_t upload_now) {
    desktop_net_slot_t *slot = (desktop_net_slot_t *)user_data;
    (void)download_total;
    (void)download_now;
    (void)upload_total;
    (void)upload_now;
    if (slot == NULL) return 0;
    return slot->cancelled ? 1 : 0;
}

static void desktop_net_free_request(desktop_net_slot_t *slot) {
    if (slot->request_headers != NULL) {
        curl_slist_free_all(slot->request_headers);
        slot->request_headers = NULL;
    }
    free(slot->request_body);
    slot->request_body = NULL;
    slot->request_body_size = 0;
}

static void desktop_net_reset_slot(desktop_net_slot_t *slot) {
    desktop_net_free_request(slot);
    free(slot->body);
    slot->body = NULL;
    slot->body_size = 0;
    slot->body_offset = 0;
    slot->response_header_count = 0;
    slot->header_storage_size = 0;
    slot->header_overflow = 0;
    slot->response_too_large = 0;
    slot->status_code = 0;
    slot->content_type[0] = '\0';
    slot->wanted_count = 0;
    slot->cancelled = 0;
    slot->complete = 0;
    slot->streaming = 0;
    slot->in_use = 0;
    slot->operation = 0;
    slot->result = PXA_STATUS_OK;
}

static void *desktop_net_worker(void *argument) {
    desktop_net_slot_t *slot = (desktop_net_slot_t *)argument;
    desktop_net_t *net = slot->net;
    CURL *curl = NULL;
    CURLcode code = CURLE_OK;
    long status_code = 0;
    char *content_type = NULL;
    pxa_status_t result = PXA_STATUS_OK;

    curl = curl_easy_init();
    if (curl == NULL) {
        result = PXA_STATUS_RESOURCE_LIMIT;
        goto done;
    }
    curl_easy_setopt(curl, CURLOPT_URL, slot->url);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)slot->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, DESKTOP_NET_MAX_REDIRECTS);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, desktop_net_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, slot);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, desktop_net_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, slot);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, desktop_net_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, slot);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    if (slot->request_headers != NULL)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slot->request_headers);
    switch (slot->method) {
        case PXA_NET_METHOD_HEAD:
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            break;
        case PXA_NET_METHOD_POST:
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            break;
        case PXA_NET_METHOD_PUT:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            break;
        case PXA_NET_METHOD_PATCH:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            break;
        case PXA_NET_METHOD_DELETE:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            break;
        default:
            break;
    }
    if (slot->request_body_size != 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, slot->request_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         (long)slot->request_body_size);
    } else if (slot->method == PXA_NET_METHOD_POST ||
               slot->method == PXA_NET_METHOD_PUT ||
               slot->method == PXA_NET_METHOD_PATCH) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    }
    code = curl_easy_perform(curl);
    if (slot->cancelled) {
        result = PXA_STATUS_CANCELLED;
    } else if (slot->response_too_large) {
        result = PXA_STATUS_RESOURCE_LIMIT;
    } else if (code != CURLE_OK) {
        result = code == CURLE_OPERATION_TIMEDOUT ? PXA_STATUS_TIMED_OUT
                                                  : PXA_STATUS_UNAVAILABLE;
    } else {
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        (void)curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
        if (content_type != NULL) {
            snprintf(slot->content_type, sizeof(slot->content_type), "%s",
                     content_type);
        }
        if (status_code <= 0 || status_code > UINT16_MAX)
            result = PXA_STATUS_UNAVAILABLE;
    }

done:
    if (curl != NULL) curl_easy_cleanup(curl);
    pthread_mutex_lock(&net->lock);
    if (slot->cancelled && !slot->complete) {
        /* The owner dropped the request while it was in flight. */
        desktop_net_reset_slot(slot);
        pthread_mutex_unlock(&net->lock);
        if (net->notify != NULL) net->notify(net->notify_context);
        return NULL;
    }
    slot->result = result;
    slot->status_code = (uint16_t)status_code;
    slot->complete = 1;
    pthread_mutex_unlock(&net->lock);
    if (net->notify != NULL) net->notify(net->notify_context);
    return NULL;
}

static desktop_net_slot_t *desktop_net_find(desktop_net_t *net,
                                            uint64_t operation) {
    size_t index;
    if (operation == 0) return NULL;
    for (index = 0; index < DESKTOP_NET_MAX_PENDING; ++index) {
        if (net->slots[index] != NULL && net->slots[index]->in_use &&
            net->slots[index]->operation == operation)
            return net->slots[index];
    }
    return NULL;
}

static pxa_status_t desktop_net_start(void *context,
                                      const pxa_net_request_t *request,
                                      uint64_t *operation) {
    desktop_net_t *net = (desktop_net_t *)context;
    desktop_net_slot_t *slot = NULL;
    pthread_t thread;
    size_t index;
    if (net == NULL || request == NULL || operation == NULL ||
        request->struct_size < sizeof(*request) || request->url.data == NULL ||
        request->url.size == 0 ||
        request->url.size >= DESKTOP_NET_MAX_URL_BYTES ||
        request->method < PXA_NET_METHOD_GET ||
        request->method > PXA_NET_METHOD_DELETE || request->max_response_bytes == 0 ||
        request->max_response_bytes > DESKTOP_NET_MAX_RESPONSE_BYTES ||
        request->timeout_ms < 100 || request->timeout_ms > 60000 ||
        request->header_count > PXA_NET_MAX_HEADERS ||
        request->wanted_response_header_count > PXA_NET_MAX_HEADERS ||
        request->body.size > DESKTOP_NET_MAX_INLINE_BODY_BYTES ||
        (request->header_count != 0 && request->headers == NULL) ||
        (request->wanted_response_header_count != 0 &&
         request->wanted_response_headers == NULL) ||
        (request->body.size != 0 && request->body.data == NULL)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&net->lock);
    for (index = 0; index < DESKTOP_NET_MAX_PENDING; ++index) {
        if (net->slots[index] == NULL) {
            net->slots[index] =
                (desktop_net_slot_t *)calloc(1, sizeof(*net->slots[index]));
            if (net->slots[index] == NULL) {
                pthread_mutex_unlock(&net->lock);
                return PXA_STATUS_RESOURCE_LIMIT;
            }
        }
        if (!net->slots[index]->in_use) {
            slot = net->slots[index];
            break;
        }
    }
    if (slot == NULL) {
        pthread_mutex_unlock(&net->lock);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    memset(slot, 0, sizeof(*slot));
    slot->net = net;
    slot->in_use = 1;
    slot->result = PXA_STATUS_OK;
    snprintf(slot->url, sizeof(slot->url), "%.*s", (int)request->url.size,
             (const char *)request->url.data);
    slot->method = request->method;
    slot->timeout_ms = (uint16_t)request->timeout_ms;
    slot->max_response_bytes = request->max_response_bytes;
    slot->deadline_ms = desktop_net_now_ms() + request->timeout_ms;
    for (index = 0; index < request->header_count; ++index) {
        char header[PXA_NET_MAX_HEADER_BLOCK_BYTES];
        const pxa_net_header_t *item = &request->headers[index];
        struct curl_slist *replacement;
        if (item->name.size + item->value.size + 3u > sizeof(header)) continue;
        snprintf(header, sizeof(header), "%.*s: %.*s", (int)item->name.size,
                 (const char *)item->name.data, (int)item->value.size,
                 (const char *)item->value.data);
        replacement = curl_slist_append(slot->request_headers, header);
        if (replacement == NULL) {
            pthread_mutex_unlock(&net->lock);
            desktop_net_reset_slot(slot);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        slot->request_headers = replacement;
    }
    if (request->body.size != 0) {
        slot->request_body = (uint8_t *)malloc(request->body.size);
        if (slot->request_body == NULL) {
            pthread_mutex_unlock(&net->lock);
            desktop_net_reset_slot(slot);
            return PXA_STATUS_RESOURCE_LIMIT;
        }
        memcpy(slot->request_body, request->body.data, request->body.size);
        slot->request_body_size = request->body.size;
    }
    for (index = 0; index < request->wanted_response_header_count; ++index) {
        pxa_bytes_t name = request->wanted_response_headers[index];
        size_t copy = name.size;
        if (copy >= DESKTOP_NET_WANTED_NAME_BYTES)
            copy = DESKTOP_NET_WANTED_NAME_BYTES - 1u;
        memcpy(slot->wanted_names[slot->wanted_count], name.data, copy);
        slot->wanted_names[slot->wanted_count][copy] = '\0';
        ++slot->wanted_count;
    }
    slot->operation = ++net->next_operation;
    if (slot->operation == 0) slot->operation = ++net->next_operation;
    *operation = slot->operation;
    pthread_mutex_unlock(&net->lock);
    if (pthread_create(&thread, NULL, desktop_net_worker, slot) != 0) {
        pthread_mutex_lock(&net->lock);
        desktop_net_reset_slot(slot);
        pthread_mutex_unlock(&net->lock);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    (void)pthread_detach(thread);
    return PXA_STATUS_OK;
}

static pxa_status_t desktop_net_poll(void *context, uint64_t operation,
                                     pxa_net_response_t *response) {
    desktop_net_t *net = (desktop_net_t *)context;
    desktop_net_slot_t *slot;
    pxa_status_t result;
    if (net == NULL || response == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    pthread_mutex_lock(&net->lock);
    slot = desktop_net_find(net, operation);
    if (slot == NULL) {
        pthread_mutex_unlock(&net->lock);
        return PXA_STATUS_NOT_FOUND;
    }
    if (!slot->complete) {
        if (!slot->cancelled && desktop_net_now_ms() >= slot->deadline_ms) {
            slot->cancelled = 1;
            pthread_mutex_unlock(&net->lock);
            return PXA_STATUS_TIMED_OUT;
        }
        pthread_mutex_unlock(&net->lock);
        return PXA_STATUS_WOULD_BLOCK;
    }
    if (slot->result != PXA_STATUS_OK) {
        result = slot->result;
        desktop_net_reset_slot(slot);
        pthread_mutex_unlock(&net->lock);
        return result;
    }
    response->status_code = slot->status_code;
    response->content_type.data = (const uint8_t *)slot->content_type;
    response->content_type.size = strlen(slot->content_type);
    response->body_stream = slot;
    response->body_length = slot->body_size;
    response->flags = PXA_NET_RESPONSE_BODY_LENGTH_KNOWN;
    if (slot->method != PXA_NET_METHOD_HEAD && slot->body_size != 0)
        response->flags |= PXA_NET_RESPONSE_BODY_PRESENT;
    response->headers = slot->response_headers;
    response->header_count = (uint16_t)slot->response_header_count;
    slot->streaming = 1;
    pthread_mutex_unlock(&net->lock);
    return PXA_STATUS_OK;
}

static void desktop_net_cancel(void *context, uint64_t operation) {
    desktop_net_t *net = (desktop_net_t *)context;
    desktop_net_slot_t *slot;
    if (net == NULL || operation == 0) return;
    pthread_mutex_lock(&net->lock);
    slot = desktop_net_find(net, operation);
    if (slot != NULL) {
        if (slot->complete || slot->streaming) {
            desktop_net_reset_slot(slot);
        } else {
            /* The worker observes the flag through curl's progress callback
             * and releases the slot when it stops. */
            slot->cancelled = 1;
        }
    }
    pthread_mutex_unlock(&net->lock);
}

static pxa_status_t desktop_net_read_body(void *context, void *body_stream,
                                          uint8_t *output, size_t capacity,
                                          size_t *size) {
    desktop_net_slot_t *slot = (desktop_net_slot_t *)body_stream;
    size_t count;
    (void)context;
    if (slot == NULL || size == NULL || (output == NULL && capacity != 0))
        return PXA_STATUS_INVALID_ARGUMENT;
    *size = 0;
    count = slot->body_size - slot->body_offset;
    if (count > capacity) count = capacity;
    if (count != 0) memcpy(output, slot->body + slot->body_offset, count);
    slot->body_offset += count;
    *size = count;
    return PXA_STATUS_OK;
}

static void desktop_net_close_body(void *context, void *body_stream) {
    desktop_net_t *net = (desktop_net_t *)context;
    desktop_net_slot_t *slot = (desktop_net_slot_t *)body_stream;
    if (net == NULL || slot == NULL) return;
    pthread_mutex_lock(&net->lock);
    if (slot->net == net && (slot->streaming || slot->complete))
        desktop_net_reset_slot(slot);
    pthread_mutex_unlock(&net->lock);
}

int pxsys_desktop_net_backend(pxa_net_backend_t *output,
                              pxsys_desktop_net_notify_fn notify,
                              void *notify_context) {
    static desktop_net_t net;
    if (output == NULL) return 0;
    pthread_once(&g_curl_once, desktop_net_curl_init);
    if (pthread_mutex_init(&net.lock, NULL) != 0) return 0;
    net.next_operation = 0;
    net.notify = notify;
    net.notify_context = notify_context;
    memset(net.slots, 0, sizeof(net.slots));
    g_desktop_net = &net;
    memset(output, 0, sizeof(*output));
    output->struct_size = sizeof(*output);
    output->context = &net;
    output->start = desktop_net_start;
    output->poll = desktop_net_poll;
    output->cancel = desktop_net_cancel;
    output->read_body = desktop_net_read_body;
    output->close_body = desktop_net_close_body;
    return 1;
}
