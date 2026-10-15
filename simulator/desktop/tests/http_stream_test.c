#define _POSIX_C_SOURCE 200809L
#include "../desktop_net.c"
#include <assert.h>
#include <stdatomic.h>

static atomic_uint notifications;
static void notified(void *context) {
    (void)context;
    atomic_fetch_add(&notifications, 1);
}
static void pause_ms(unsigned ms) {
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}
static int32_t wait_io(pxa_net_backend_t *b, void *exchange, uint32_t op, uint8_t *data,
                       size_t size) {
    uint64_t until = desktop_net_now_ms() + 10000;
    int32_t n;
    do {
        n = b->exchange_io(b->context, exchange, op, data, size);
        if (n != PXA_STATUS_WOULD_BLOCK)
            return n;
        assert(pxsys_desktop_net_wait(b, 1000));
    } while (desktop_net_now_ms() < until);
    desktop_net_slot_t *slot = exchange;
    fprintf(stderr,
            "wait timeout op=%u state=%u error=%d headers=%u receive=%u complete=%u\n",
            op, pxa_http_stream_state(&desktop_http(slot)->stream),
            desktop_http(slot)->stream.error, desktop_http(slot)->stream.head_size,
            pxa_http_ring_readable(&desktop_http(slot)->stream.receive),
            slot->complete);
    assert(0);
    return -1;
}
static void wait_cleanup(void) {
    for (unsigned retry = 0; retry < 2000; ++retry) {
        unsigned active = 0;
        pthread_mutex_lock(&g_desktop_net->lock);
        for (unsigned i = 0; i < DESKTOP_NET_MAX_PENDING; ++i)
            active += g_desktop_net->slots[i] && g_desktop_net->slots[i]->in_use;
        pthread_mutex_unlock(&g_desktop_net->lock);
        if (!active)
            return;
        pause_ms(1);
    }
    assert(0);
}
static pxa_net_request_t request(const char *url) {
    pxa_net_request_t r = {0};
    r.struct_size = sizeof(r);
    r.url = (pxa_bytes_t){(const uint8_t *)url, strlen(url)};
    r.method = PXA_NET_METHOD_GET;
    r.timeout_ms = 10000;
    r.abi_minor = 2;
    r.max_response_bytes = UINT32_MAX;
    r.buffer_bytes = 4096;
    r.response_header_bytes = 1024;
    r.max_body_bytes = 2 * 1024 * 1024;
    return r;
}
static void download(pxa_net_backend_t *b, const char *base, int chunked, int slow) {
    char url[256];
    uint8_t block[2048], head[1024];
    void *exchange;
    uint64_t begin = desktop_net_now_ms(), head_ms, first_ms = 0;
    size_t total = 0;
    snprintf(url, sizeof(url), "%s/%s", base, chunked ? "chunked" : "large");
    pxa_net_request_t r = request(url);
    assert(b->open_exchange(b->context, &r, &exchange) == PXA_STATUS_OK);
    assert(wait_io(b, exchange, PXA_HTTP_IO_RESPONSE, head, sizeof(head)) > 16);
    head_ms = desktop_net_now_ms() - begin;
    assert(pxa_read_u16(head) == 200);
    unsigned cookies = 0;
    for (size_t at = 16; at < 16 + pxa_read_u32(head + 4);) {
        uint16_t n = pxa_read_u16(head + at), v = pxa_read_u16(head + at + 2);
        if (n == 10 && !memcmp(head + at + 4, "set-cookie", 10))
            ++cookies;
        at += 4u + n + v;
    }
    assert(cookies == 2);
    if (slow)
        pause_ms(100);
    for (;;) {
        int32_t n = wait_io(b, exchange, PXA_IO_READ, block, sizeof(block));
        assert(n >= 0);
        if (!n)
            break;
        if (!first_ms)
            first_ms = desktop_net_now_ms() - begin;
        for (int32_t i = 0; i < n; ++i)
            assert(block[i] == (uint8_t)((total + (size_t)i) * 31u + 7u));
        total += (size_t)n;
        if (slow)
            pause_ms(1);
    }
    assert(total == 1024 * 1024);
    printf("stream chunked=%d slow=%d head_ms=%llu first_ms=%llu total_ms=%llu "
           "bytes=%zu storage=%zu notifications=%u\n",
           chunked, slow, (unsigned long long)head_ms, (unsigned long long)first_ms,
           (unsigned long long)(desktop_net_now_ms() - begin), total,
           sizeof(desktop_http_t) +
               pxa_http_stream_storage_size(r.buffer_bytes, r.response_header_bytes),
           atomic_load(&notifications));
    b->close_exchange(b->context, exchange);
    wait_cleanup();
}
static void compare_buffered(pxa_net_backend_t *b, const char *base) {
    char url[256];
    uint8_t bytes[4096];
    uint64_t operation, start, first;
    pxa_net_response_t response = {0};
    size_t total = 0, n;
    snprintf(url, sizeof(url), "%s/baseline", base);
    pxa_net_request_t r = request(url);
    r.abi_minor = 1;
    r.max_response_bytes = 262144;
    start = desktop_net_now_ms();
    assert(b->start(b->context, &r, &operation) == PXA_STATUS_OK);
    while (b->poll(b->context, operation, &response) == PXA_STATUS_WOULD_BLOCK)
        pause_ms(1);
    first = desktop_net_now_ms() - start;
    desktop_net_slot_t *slot = response.body_stream;
    assert(slot && response.status_code == 200);
    size_t body_storage = slot->body_size;
    for (;;) {
        assert(b->read_body(b->context, slot, bytes, sizeof(bytes), &n) ==
               PXA_STATUS_OK);
        if (!n)
            break;
        for (size_t i = 0; i < n; ++i)
            assert(bytes[i] == (uint8_t)((total + i) * 31u + 7u));
        total += n;
    }
    assert(total == 262144);
    printf("baseline buffered first_ms=%llu total_ms=%llu bytes=%zu body_storage=%zu\n",
           (unsigned long long)first,
           (unsigned long long)(desktop_net_now_ms() - start), total, body_storage);
    b->close_body(b->context, slot);
    wait_cleanup();
    r = request(url);
    void *exchange;
    uint8_t head[1024];
    total = 0;
    start = desktop_net_now_ms();
    assert(b->open_exchange(b->context, &r, &exchange) == PXA_STATUS_OK);
    assert(wait_io(b, exchange, PXA_HTTP_IO_RESPONSE, head, sizeof(head)) > 16);
    uint64_t header_ms = desktop_net_now_ms() - start;
    first = 0;
    for (;;) {
        int32_t amount = wait_io(b, exchange, PXA_IO_READ, bytes, sizeof(bytes));
        assert(amount >= 0);
        if (!amount)
            break;
        if (!first)
            first = desktop_net_now_ms() - start;
        for (int32_t i = 0; i < amount; ++i)
            assert(bytes[i] == (uint8_t)((total + (size_t)i) * 31u + 7u));
        total += (unsigned)amount;
    }
    assert(total == 262144);
    printf("baseline streamed header_ms=%llu first_ms=%llu total_ms=%llu bytes=%zu "
           "body_storage=%u\n",
           (unsigned long long)header_ms, (unsigned long long)first,
           (unsigned long long)(desktop_net_now_ms() - start), total, r.buffer_bytes);
    b->close_exchange(b->context, exchange);
    wait_cleanup();
}
int main(int argc, char **argv) {
    pxa_net_backend_t b;
    void *exchange;
    uint8_t bytes[4096], head[1024];
    char url[256];
    setbuf(stdout, NULL);
    assert(argc == 2 && pxsys_desktop_net_backend(&b, notified, NULL));
    assert(!pxsys_desktop_net_wait(&b, 1)); /* Idle backend needs no extra waiter. */
    compare_buffered(&b, argv[1]);
    download(&b, argv[1], 0, 0);
    download(&b, argv[1], 1, 1);
    snprintf(url, sizeof(url), "%s/upload", argv[1]);
    pxa_net_request_t r = request(url);
    r.method = PXA_NET_METHOD_POST;
    r.upload_bytes = 100 * 1024;
    assert(b.open_exchange(b.context, &r, &exchange) == PXA_STATUS_OK);
    for (size_t total = 0; total < r.upload_bytes;) {
        size_t count = r.upload_bytes - total;
        if (count > sizeof(bytes))
            count = sizeof(bytes);
        for (size_t i = 0; i < count; ++i)
            bytes[i] = (uint8_t)((total + i) * 31u + 7u);
        int32_t n = wait_io(&b, exchange, PXA_IO_WRITE, bytes, count);
        assert(n > 0);
        total += (unsigned)n;
    }
    assert(b.exchange_io(b.context, exchange, PXA_HTTP_IO_FINISH_UPLOAD, NULL, 0) == 0);
    assert(wait_io(&b, exchange, PXA_HTTP_IO_RESPONSE, head, sizeof(head)) > 16 &&
           pxa_read_u16(head) == 200);
    int32_t n = wait_io(&b, exchange, PXA_IO_READ, bytes, sizeof(bytes));
    assert(n == 6 && !memcmp(bytes, "102400", 6));
    b.close_exchange(b.context, exchange);
    wait_cleanup();
    puts("upload bytes=102400 bounded-buffer PASS");
    // Cancel while the producer is blocked on a full receive ring.
    snprintf(url, sizeof(url), "%s/large", argv[1]);
    r = request(url);
    assert(b.open_exchange(b.context, &r, &exchange) == PXA_STATUS_OK);
    assert(wait_io(&b, exchange, PXA_HTTP_IO_RESPONSE, head, sizeof(head)) > 16);
    pause_ms(30);
    b.close_exchange(b.context, exchange);
    wait_cleanup();
    puts("cancel-full-ring PASS");
    r.max_body_bytes = 100;
    assert(b.open_exchange(b.context, &r, &exchange) == PXA_STATUS_OK);
    assert(wait_io(&b, exchange, PXA_HTTP_IO_RESPONSE, head, sizeof(head)) ==
           PXA_STATUS_LIMIT_EXCEEDED);
    b.close_exchange(b.context, exchange);
    wait_cleanup();
    puts("response-budget PASS");
    snprintf(url, sizeof(url), "%s/redirect", argv[1]);
    r = request(url);
    assert(b.open_exchange(b.context, &r, &exchange) == PXA_STATUS_OK);
    assert(wait_io(&b, exchange, PXA_HTTP_IO_RESPONSE, head, sizeof(head)) > 16 &&
           pxa_read_u16(head) == 302);
    b.close_exchange(b.context, exchange);
    wait_cleanup();
    puts("redirect-returned PASS");
    // A rejecting server can send a final response before the upload finishes.
    snprintf(url, sizeof(url), "%s/reject", argv[1]);
    r = request(url);
    r.method = PXA_NET_METHOD_POST;
    r.upload_bytes = 1024 * 1024;
    assert(b.open_exchange(b.context, &r, &exchange) == PXA_STATUS_OK);
    assert(wait_io(&b, exchange, PXA_HTTP_IO_RESPONSE, head, sizeof(head)) > 16 &&
           pxa_read_u16(head) == 413);
    b.close_exchange(b.context, exchange);
    wait_cleanup();
    puts("early-upload-response PASS");
    for (unsigned i = 0; i < 2; ++i) {
        snprintf(url, sizeof(url), "%s/%s", argv[1],
                 i ? "conflicting-length" : "ambiguous");
        r = request(url);
        assert(b.open_exchange(b.context, &r, &exchange) == PXA_STATUS_OK);
        int32_t rejected =
            wait_io(&b, exchange, PXA_HTTP_IO_RESPONSE, head, sizeof(head));
        assert(rejected == PXA_STATUS_PROTOCOL_ERROR ||
               rejected == PXA_STATUS_IO_ERROR);
        b.close_exchange(b.context, exchange);
        wait_cleanup();
    }
    puts("ambiguous-framing PASS");
    pxsys_desktop_net_shutdown(&b);
    return 0;
}
