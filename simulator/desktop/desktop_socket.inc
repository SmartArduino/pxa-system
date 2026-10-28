/* SPDX-License-Identifier: MIT
 * Included by desktop_net.c. Curl CONNECT_ONLY owns TCP/TLS setup and trust;
 * raw bytes after connect stay independent of HTTP and application protocols.
 */
#include "pxa/socket_stream.h"
struct desktop_socket {
  struct desktop_socket *next;
  desktop_net_t *net;
  pxa_socket_stream_t stream;
  uint32_t references, timeout_ms;
  char url[320];
  uint8_t storage[];
};
static void desktop_socket_release(struct desktop_socket *s) {
  if (__atomic_sub_fetch(&s->references, 1, __ATOMIC_ACQ_REL) == 0)
    free(s);
}
static int desktop_socket_cancel(void *context, curl_off_t a, curl_off_t b,
                                 curl_off_t c, curl_off_t d) {
  (void)a;
  (void)b;
  (void)c;
  (void)d;
  struct desktop_socket *s = context;
  return !!(__atomic_load_n(&s->stream.state, __ATOMIC_ACQUIRE) &
            PXA_SOCKET_CLOSED);
}
static void *desktop_socket_worker(void *context) {
  struct desktop_socket *s = context;
  CURL *curl = curl_easy_init();
  int32_t error = PXA_STATUS_IO_ERROR;
  if (!curl) {
    error = PXA_STATUS_RESOURCE_LIMIT;
    goto done;
  }
  curl_easy_setopt(curl, CURLOPT_URL, s->url);
  curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(curl, CURLOPT_PROXY,
                   ""); /* direct sockets do not inherit HTTP proxy settings */
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)s->timeout_ms);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, desktop_socket_cancel);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, s);
  CURLcode connected = curl_easy_perform(curl);
  if (connected != CURLE_OK) {
    if (connected == CURLE_OPERATION_TIMEDOUT)
      error = PXA_STATUS_TIMED_OUT;
    goto done;
  }
  __atomic_fetch_or(&s->stream.state, PXA_SOCKET_CONNECTED, __ATOMIC_RELEASE);
  desktop_net_notify(s->net);
  while (!desktop_socket_cancel(s, 0, 0, 0, 0)) {
    uint8_t *data;
    size_t size = pxa_http_ring_peek(&s->stream.send, &data, 0);
    if (size) {
      size_t sent = 0;
      CURLcode r = curl_easy_send(curl, data, size, &sent);
      if (r != CURLE_OK && r != CURLE_AGAIN)
        goto done;
      if (sent) {
        pxa_http_ring_advance(&s->stream.send, sent, 0);
        desktop_net_notify(s->net);
      }
    }
    size = pxa_http_ring_peek(&s->stream.receive, &data, 1);
    if (size) {
      size_t got = 0;
      CURLcode r = curl_easy_recv(curl, data, size, &got);
      if (r != CURLE_OK && r != CURLE_AGAIN)
        goto done;
      if (r == CURLE_OK && !got) {
        error = PXA_STATUS_OK;
        goto done;
      }
      if (got) {
        pxa_http_ring_advance(&s->stream.receive, got, 1);
        desktop_net_notify(s->net);
      }
    }
    struct timespec delay = {0, 5000000};
    nanosleep(&delay, NULL);
  }
done:
  if (curl)
    curl_easy_cleanup(curl);
  pxa_socket_stream_finish(&s->stream, error);
  desktop_net_notify(s->net);
  desktop_net_t *net = s->net;
  pthread_mutex_lock(&net->lock);
  struct desktop_socket **p = &net->sockets;
  while (*p && *p != s)
    p = &(*p)->next;
  if (*p)
    *p = s->next;
  net->workers--;
  pthread_mutex_unlock(&net->lock);
  desktop_socket_release(s);
  return NULL;
}
static pxa_status_t desktop_socket_open(void *context,
                                        const pxa_socket_request_t *request,
                                        void **output) {
  desktop_net_t *net = context;
  *output = NULL;
  struct desktop_socket *s = calloc(1, sizeof(*s) + request->buffer_bytes);
  if (!s)
    return PXA_STATUS_RESOURCE_LIMIT;
  if (!pxa_socket_stream_init(&s->stream, s->storage, request->buffer_bytes)) {
    free(s);
    return PXA_STATUS_INVALID_ARGUMENT;
  }
  s->net = net;
  s->references = 2;
  s->timeout_ms = request->connect_timeout_ms;
  snprintf(s->url, sizeof(s->url), "%s://%.*s/",
           request->tls ? "https" : "http", (int)(request->endpoint.size - 6),
           request->endpoint.data + 6);
  pthread_mutex_lock(&net->lock);
  unsigned count = 0;
  for (struct desktop_socket *i = net->sockets; i; i = i->next)
    count++;
  if (count >= DESKTOP_NET_MAX_PENDING) {
    pthread_mutex_unlock(&net->lock);
    free(s);
    return PXA_STATUS_RESOURCE_LIMIT;
  }
  s->next = net->sockets;
  net->sockets = s;
  net->workers++;
  pthread_t worker;
  if (pthread_create(&worker, NULL, desktop_socket_worker, s)) {
    net->sockets = s->next;
    net->workers--;
    pthread_mutex_unlock(&net->lock);
    free(s);
    return PXA_STATUS_RESOURCE_LIMIT;
  }
  pthread_detach(worker);
  pthread_mutex_unlock(&net->lock);
  *output = s;
  return PXA_STATUS_OK;
}
static int32_t desktop_socket_io(void *context, void *stream,
                                 uint32_t operation, uint8_t *data,
                                 size_t size) {
  (void)context;
  struct desktop_socket *s = stream;
  return pxa_socket_stream_io(&s->stream, operation, data, size);
}
static void desktop_socket_close(void *context, void *stream) {
  (void)context;
  struct desktop_socket *s = stream;
  __atomic_fetch_or(&s->stream.state, PXA_SOCKET_CLOSED, __ATOMIC_RELEASE);
  desktop_socket_release(s);
}
