/* SPDX-License-Identifier: MIT
 * Bounded IPv4 local-network provider. One shared worker per Host, no worker
 * per connection. Native I/O and mDNS parsing never execute in Guest imports.
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include "pxa_lan_native.h"
#include "mdns_probe.h"
#include "pxa/socket_stream.h"
#include "vendor/mdns/mdns.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
typedef SemaphoreHandle_t lan_mutex_t;
#define LOCK(c) xSemaphoreTake((c)->mutex, portMAX_DELAY)
#define UNLOCK(c) xSemaphoreGive((c)->mutex)
static void *lan_alloc(size_t n) {
  void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(1, n, MALLOC_CAP_8BIT);
}
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
#else
#include <pthread.h>
typedef pthread_mutex_t lan_mutex_t;
#define LOCK(c) pthread_mutex_lock(&(c)->mutex)
#define UNLOCK(c) pthread_mutex_unlock(&(c)->mutex)
static void *lan_alloc(size_t n) { return calloc(1, n); }
static uint64_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
#endif
#define LAN_MAX_OBJECTS 16
#define LAN_PACKET 1200
#define LAN_EVENT 512
#define LAN_CACHE 8
#define LAN_DNS_BYTES 2048
struct lan_context;
typedef struct lan_discovery {
  uint32_t revisions[LAN_CACHE];
  char type[80], instance[160];
  uint8_t txt[256];
  uint16_t txt_size;
  uint64_t next_query, started;
  uint32_t query_interval;
  uint8_t probes, announcements, conflict, packet_seen;
  struct sockaddr_in reply_address;
  char reply_name[160];
  uint16_t reply_id, reply_type;
  uint8_t reply_pending, visible;
} lan_discovery;
typedef struct lan_object {
  struct lan_context *ctx;
  struct lan_object *parent;
  pxa_socket_stream_t stream;
  pxa_net_endpoint_t local, peer;
  int fd, error;
  uint16_t kind, flags, max_items, children;
  uint8_t ready, closed, claimed, announced, network_scope;
  uint32_t bytes;
  uint64_t received, sent;
  uint32_t dropped;

  struct lan_discovery *discovery;
  uint8_t storage[];
} lan_object;
typedef struct {
  char instance[160], host[160];
  uint32_t address;
  uint16_t port, txt_size;
  uint8_t txt[256], used, complete;
  uint64_t expires, next_resolve;
  uint32_t hash, revision;
} lan_service;
typedef struct lan_context {
  pxa_lan_native_config_t config;
  lan_mutex_t mutex;
  lan_object *objects[LAN_MAX_OBJECTS];
  lan_service *cache;
  int mdns_fd;
  unsigned running, stopping, changed;
  char hostname[64];
  uint32_t interface_address, interface_mask, random_state;
  uint64_t next_interface_check;
  uint8_t *packet;
#ifndef ESP_PLATFORM
  pthread_t worker;
  int worker_valid;
#endif
} lan_context;
static int start_worker(lan_context *);
static uint32_t next_random(lan_context *c) {
  uint32_t x = c->random_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return c->random_state = x ? x : UINT32_C(0xa5719d3b);
}
static int is_local(lan_context *c, uint32_t address) {
  return (ntohl(address) >> 24) == 127 ||
         (c->interface_address && c->interface_mask &&
          (address & c->interface_mask) ==
              (c->interface_address & c->interface_mask));
}
static int fd_nonblocking(int fd) {
  if (fd < 0 || fd >= FD_SETSIZE)
    return 0;
  int f = fcntl(fd, F_GETFL, 0);
  return f >= 0 && fcntl(fd, F_SETFL, f | O_NONBLOCK) == 0;
}
static int transient_error(void) {
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
static struct sockaddr_in endpoint_addr(const pxa_net_endpoint_t *e) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(e->port);
  memcpy(&a.sin_addr.s_addr, e->address, 4);
  return a;
}
static void from_addr(pxa_net_endpoint_t *e, const struct sockaddr_in *a) {
  memset(e, 0, sizeof(*e));
  e->family = 4;
  e->port = ntohs(a->sin_port);
  memcpy(e->address, &a->sin_addr.s_addr, 4);
}
static void ring_copy(pxa_http_ring_t *r, size_t offset, void *out, size_t n) {
  uint8_t *p = out;
  for (size_t i = 0; i < n; ++i)
    p[i] = r->data[(r->read_position + (uint32_t)offset + (uint32_t)i) &
                   (r->capacity - 1)];
}
static void ring_put(pxa_http_ring_t *r, const void *data, size_t n) {
  const uint8_t *p = data;
  while (n) {
    uint8_t *out;
    size_t m = pxa_http_ring_peek(r, &out, 1);
    if (m > n)
      m = n;
    memcpy(out, p, m);
    pxa_http_ring_advance(r, m, 1);
    p += m;
    n -= m;
  }
}
static int packet_put(pxa_http_ring_t *r, const uint8_t *p, size_t n) {
  if (n > UINT16_MAX || pxa_http_ring_writable(r) < n + 2)
    return 0;
  uint8_t h[2];
  pxa_write_u16(h, (uint16_t)n);
  ring_put(r, h, 2);
  ring_put(r, p, n);
  return 1;
}
static size_t packet_size(pxa_http_ring_t *r) {
  uint8_t h[2];
  if (pxa_http_ring_readable(r) < 2)
    return 0;
  ring_copy(r, 0, h, 2);
  return pxa_read_u16(h);
}
static int mdns_send(lan_object *, int);
static void mdns_step(lan_context *, uint64_t);
static int mdns_open(lan_context *c) {
  if (c->mdns_fd >= 0)
    return 1;
  if (!c->cache)
    c->cache = lan_alloc(sizeof(lan_service) * LAN_CACHE);
  if (!c->packet)
    c->packet = lan_alloc(LAN_DNS_BYTES);
  if (!c->cache || !c->packet)
    return 0;
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(5353);
  a.sin_addr.s_addr = c->interface_address;
  c->mdns_fd = mdns_socket_open_ipv4(&a);
  if (c->mdns_fd >= FD_SETSIZE) {
    mdns_socket_close(c->mdns_fd);
    c->mdns_fd = -1;
  }
  if (c->mdns_fd >= 0) {
    unsigned char ttl = 255;
    (void)setsockopt(c->mdns_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                     sizeof(ttl));
  }
  return c->mdns_fd >= 0;
}
static lan_object *object_new(lan_context *c, uint16_t kind, uint32_t bytes) {
  unsigned i;
  for (i = 0; i < c->config.max_resources; ++i)
    if (!c->objects[i])
      break;
  if (i == c->config.max_resources)
    return NULL;
  if (kind == PXA_LAN_ADVERTISE)
    bytes = 512;
  lan_object *o = lan_alloc(sizeof(*o) + bytes);
  if (!o)
    return NULL;
  o->ctx = c;
  o->fd = -1;
  o->kind = kind;
  o->bytes = bytes;
  if (!pxa_socket_stream_init(&o->stream, o->storage, bytes)) {
    free(o);
    return NULL;
  }
  if (kind == PXA_LAN_ADVERTISE || kind == PXA_LAN_BROWSE) {
    o->discovery = lan_alloc(sizeof(lan_discovery));
    if (!o->discovery) {
      free(o);
      return NULL;
    }
  }
  c->objects[i] = o;
  return o;
}
static void init_socket(lan_object *o) {
  lan_context *c = o->ctx;
  uint32_t address;
  memcpy(&address, o->local.address, 4);
  if (!address) {
    address = c->interface_address;
    memcpy(o->local.address, &address, 4);
  }
  if (!address ||
      (address != c->interface_address && (ntohl(address) >> 24) != 127)) {
    o->error = PXA_STATUS_DENIED;
    return;
  }
  int type = o->kind == PXA_LAN_LISTENER ? SOCK_STREAM : SOCK_DGRAM;
  int fd = socket(AF_INET, type, 0);
  if (fd < 0) {
    o->error = PXA_STATUS_RESOURCE_LIMIT;
    return;
  }
  int reuse = 1;
  if (type == SOCK_STREAM)
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  if (o->flags & PXA_LAN_BROADCAST)
    (void)setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &reuse, sizeof(reuse));
  struct sockaddr_in a = endpoint_addr(&o->local);
  if (!fd_nonblocking(fd) || bind(fd, (struct sockaddr *)&a, sizeof(a)) ||
      (type == SOCK_STREAM && listen(fd, o->max_items))) {
    close(fd);
    o->error = PXA_STATUS_IO_ERROR;
    return;
  }
  socklen_t n = sizeof(a);
  if (getsockname(fd, (struct sockaddr *)&a, &n)) {
    close(fd);
    o->error = PXA_STATUS_IO_ERROR;
    return;
  }
  from_addr(&o->local, &a);
  o->fd = fd;
  o->ready = 1;
  c->changed = 1;
}
static int readable_fd(int fd) {
  if (fd < 0 || fd >= FD_SETSIZE)
    return 0;
  fd_set set;
  FD_ZERO(&set);
  FD_SET(fd, &set);
  struct timeval t = {0, 0};
  return select(fd + 1, &set, NULL, NULL, &t) > 0;
}
static void stream_step(lan_object *o) {
  uint8_t *p;
  size_t n = pxa_http_ring_peek(&o->stream.send, &p, 0);
  if (n) {
    ssize_t m = send(o->fd, p, n,
#ifdef MSG_NOSIGNAL
                     MSG_NOSIGNAL
#else
                     0
#endif
    );
    if (m > 0) {
      pxa_http_ring_advance(&o->stream.send, (size_t)m, 0);
      o->sent += (uint64_t)m;
      o->ctx->changed = 1;
    } else if (m < 0 && !transient_error())
      o->error = PXA_STATUS_IO_ERROR;
  }
  n = pxa_http_ring_peek(&o->stream.receive, &p, 1);
  if (n) {
    ssize_t m = recv(o->fd, p, n, 0);
    if (m > 0) {
      pxa_http_ring_advance(&o->stream.receive, (size_t)m, 1);
      o->received += (uint64_t)m;
      o->ctx->changed = 1;
    } else if (m == 0) {
      o->ready = 0;
      pxa_socket_stream_finish(&o->stream, 0);
      o->ctx->changed = 1;
    } else if (!transient_error())
      o->error = PXA_STATUS_IO_ERROR;
  }
}
static void listener_step(lan_object *o) {
  if (o->children >= o->max_items || !readable_fd(o->fd))
    return;
  lan_object *child = object_new(o->ctx, PXA_LAN_STREAM, o->bytes);
  if (!child)
    return;
  struct sockaddr_in peer;
  socklen_t n = sizeof(peer);
  int fd = accept(o->fd, (struct sockaddr *)&peer, &n);
  if (fd < 0) {
    child->closed = 1;
    return;
  }
  if ((!o->network_scope && !is_local(o->ctx, peer.sin_addr.s_addr)) ||
      !fd_nonblocking(fd)) {
    close(fd);
    child->closed = 1;
    return;
  }
#ifdef SO_NOSIGPIPE
  int no_signal = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_signal, sizeof(no_signal));
#endif
  if (o->flags & PXA_LAN_NO_DELAY) {
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  }
  child->network_scope = o->network_scope;
  child->fd = fd;
  child->ready = 1;
  child->parent = o;
  child->local = o->local;
  from_addr(&child->peer, &peer);
  child->stream.state = PXA_SOCKET_CONNECTED;
  ++o->children;
  o->ctx->changed = 1;
}
static void datagram_step(lan_object *o) {
  uint8_t data[LAN_PACKET + 25];
  size_t n = packet_size(&o->stream.send);
  if (n) {
    ring_copy(&o->stream.send, 2, data, n);
    pxa_net_endpoint_t e;
    if (!pxa_net_endpoint_decode(data, n, &e)) {
      struct sockaddr_in to = endpoint_addr(&e);
      ssize_t m = sendto(o->fd, data + 24, n - 24, 0, (struct sockaddr *)&to,
                         sizeof(to));
      if (m >= 0) {
        pxa_http_ring_advance(&o->stream.send, n + 2, 0);
        o->sent += (uint64_t)m;
        o->ctx->changed = 1;
      } else if (!transient_error()) {
        pxa_http_ring_advance(&o->stream.send, n + 2, 0);
        ++o->dropped;
        o->ctx->changed = 1;
      }
    }
  }
  for (unsigned i = 0; i < 4; ++i) {
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    ssize_t m = recvfrom(o->fd, data + 24, LAN_PACKET + 1, 0,
                         (struct sockaddr *)&from, &len);
    if (m < 0)
      break;
    if (m > LAN_PACKET) {
      ++o->dropped;
      continue;
    }
    if (!o->network_scope && !is_local(o->ctx, from.sin_addr.s_addr)) {
      ++o->dropped;
      continue;
    }
    pxa_net_endpoint_t e;
    from_addr(&e, &from);
    pxa_net_endpoint_encode(data, &e);
    if (!packet_put(&o->stream.receive, data, (size_t)m + 24))
      ++o->dropped;
    else {
      o->received += (uint64_t)m;
      o->ctx->changed = 1;
    }
  }
}
static void cleanup_objects(lan_context *c) {
  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (!o || !o->closed)
      continue;
    if (o->fd >= 0) {
      close(o->fd);
      o->fd = -1;
    }
    if (o->kind == PXA_LAN_ADVERTISE && o->announced) {
      mdns_send(o, 1);
      o->announced = 0;
    }
    for (unsigned j = 0; j < c->config.max_resources; ++j)
      if (c->objects[j] && c->objects[j]->parent == o &&
          !c->objects[j]->claimed)
        c->objects[j]->closed = 1;
    if (o->children)
      continue;
    if (o->parent)
      --o->parent->children;
    c->objects[i] = NULL;
    free(o->discovery);
    free(o);
  }
}
static void worker_step(lan_context *c) {
  uint64_t now = now_ms();
  uint32_t address = 0, mask = 0;
  if (now >= c->next_interface_check) {
    c->next_interface_check = now + 1000;
    if (c->config.interface(c->config.context, &address, &mask) && address &&
        mask) {
      if (c->interface_address && c->interface_address != address) {
        for (unsigned i = 0; i < c->config.max_resources; ++i)
          if (c->objects[i] && !c->objects[i]->closed)
            c->objects[i]->error = PXA_STATUS_UNAVAILABLE;
        c->changed = 1;
      }
      c->interface_address = address;
      c->interface_mask = mask;
    } else {
      for (unsigned i = 0; i < c->config.max_resources; ++i)
        if (c->objects[i] && !c->objects[i]->closed) {
          c->objects[i]->error = PXA_STATUS_UNAVAILABLE;
          c->changed = 1;
        }
    }
  }
  cleanup_objects(c);
  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (!o || o->closed)
      continue;
    if (o->error) {
      if (o->kind == PXA_LAN_STREAM && !(o->stream.state & PXA_SOCKET_DONE))
        pxa_socket_stream_finish(&o->stream, o->error);
      continue;
    }
    if (o->kind == PXA_LAN_LISTENER || o->kind == PXA_LAN_DATAGRAM) {
      if (!o->ready)
        init_socket(o);
      if (o->error) {
        c->changed = 1;
        continue;
      }
      if (o->kind == 1)
        listener_step(o);
      else
        datagram_step(o);
    } else if (o->kind == PXA_LAN_STREAM && o->ready)
      stream_step(o);
    if (o->kind == PXA_LAN_STREAM && o->error) {
      pxa_socket_stream_finish(&o->stream, o->error);
      o->ready = 0;
      c->changed = 1;
    } else if (o->kind >= PXA_LAN_ADVERTISE && o->kind <= PXA_LAN_BROWSE &&
               !o->ready) {
      uint32_t requested_address;
      memcpy(&requested_address, o->local.address, 4);
      if (requested_address && requested_address != c->interface_address) {
        o->error = PXA_STATUS_DENIED;
        c->changed = 1;
      } else if (!mdns_open(c)) {
        o->error = PXA_STATUS_IO_ERROR;
        c->changed = 1;
      } else {
        if (o->kind == PXA_LAN_BROWSE) {
          o->ready = 1;
          c->changed = 1;
        }
      }
    }
  }
  mdns_step(c, now);
}
static int idle_worker(lan_context *c) {
  for (unsigned i = 0; i < c->config.max_resources; ++i)
    if (c->objects[i])
      return 0;
  c->running = 0;
  return 1;
}
#ifdef ESP_PLATFORM
static void worker(void *p) {
  lan_context *c = p;
  for (;;) {
    LOCK(c);
    if (c->stopping) {
      c->running = 0;
      UNLOCK(c);
      break;
    }
    worker_step(c);
    if (idle_worker(c)) {
      UNLOCK(c);
      break;
    }
    unsigned changed = c->changed;
    UNLOCK(c);
    if (changed)
      c->config.notify(c->config.context);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelete(NULL);
}
#else
static void *worker(void *p) {
  lan_context *c = p;
  for (;;) {
    LOCK(c);
    if (c->stopping) {
      c->running = 0;
      UNLOCK(c);
      break;
    }
    worker_step(c);
    if (idle_worker(c)) {
      UNLOCK(c);
      break;
    }
    unsigned changed = c->changed;
    UNLOCK(c);
    if (changed)
      c->config.notify(c->config.context);
    struct timespec delay = {0, 10000000};
    nanosleep(&delay, NULL);
  }
  return NULL;
}
#endif
static int start_worker(lan_context *c) {
  if (c->running)
    return 1;
#ifndef ESP_PLATFORM
  if (c->worker_valid) {
    pthread_join(c->worker, NULL);
    c->worker_valid = 0;
  }
#endif
  c->running = 1;
#ifdef ESP_PLATFORM
  if (xTaskCreate(worker, "pxa_lan", 6144, c, 4, NULL) != pdPASS) {
    c->running = 0;
    return 0;
  }
#else
  if (pthread_create(&c->worker, NULL, worker, c)) {
    c->running = 0;
    return 0;
  }
  c->worker_valid = 1;
#endif
  return 1;
}
static int32_t local_io(void *context, void *object, uint32_t op, uint8_t *data,
                        size_t size) {
  lan_context *c = context;
  lan_object *o = object;
  int32_t result = PXA_STATUS_UNSUPPORTED;
  LOCK(c);
  if (o->closed) {
    result = PXA_STATUS_NOT_FOUND;
    goto out;
  }
  if (op == PXA_LAN_IO_STATS) {
    if (size < 32) {
      result = PXA_STATUS_INVALID_ARGUMENT;
      goto out;
    }
    pxa_write_u64(data, o->received);
    pxa_write_u64(data + 8, o->sent);
    pxa_write_u32(data + 16, o->dropped);
    pxa_write_u32(data + 20, pxa_http_ring_readable(&o->stream.receive));
    pxa_write_u32(data + 24, pxa_http_ring_readable(&o->stream.send));
    pxa_write_u32(data + 28, o->bytes);
    result = 32;
    goto out;
  }
  if (op == PXA_IO_POLL) {
    uint32_t ready = 0;
    if (o->kind == PXA_LAN_STREAM) {
      result = pxa_socket_stream_io(&o->stream, op, data, size);
      goto out;
    }
    if (o->error)
      ready = PXA_READY_READ | PXA_READY_WRITE | PXA_READY_END;
    else if (o->ready) {
      ready = o->kind != PXA_LAN_DATAGRAM ||
                      pxa_http_ring_writable(&o->stream.send) >= 26
                  ? PXA_READY_WRITE
                  : 0;
      if (o->kind == PXA_LAN_LISTENER) {
        for (unsigned i = 0; i < c->config.max_resources; ++i)
          if (c->objects[i] && c->objects[i]->parent == o &&
              !c->objects[i]->claimed && !c->objects[i]->closed)
            ready |= PXA_READY_READ;
      } else if (pxa_http_ring_readable(&o->stream.receive))
        ready |= PXA_READY_READ;
      if (o->kind == PXA_LAN_STREAM)
        ready = (uint32_t)pxa_socket_stream_io(&o->stream, op, data, size);
    }
    result = (int32_t)ready;
    goto out;
  }
  if (op == PXA_LAN_IO_LOCAL || op == PXA_LAN_IO_PEER) {
    if (size < 24) {
      result = PXA_STATUS_INVALID_ARGUMENT;
      goto out;
    }
    if (o->error) {
      result = o->error;
      goto out;
    }
    if (!o->ready) {
      result = PXA_STATUS_WOULD_BLOCK;
      goto out;
    }
    if (op == PXA_LAN_IO_PEER && o->kind != PXA_LAN_STREAM) {
      result = PXA_STATUS_UNSUPPORTED;
      goto out;
    }
    pxa_net_endpoint_encode(data,
                            op == PXA_LAN_IO_LOCAL ? &o->local : &o->peer);
    result = 24;
    goto out;
  }
  if (o->kind == PXA_LAN_STREAM) {
    result = pxa_socket_stream_io(&o->stream, op, data, size);
    goto out;
  }
  if (o->error) {
    result = o->error;
    goto out;
  }
  if (!o->ready) {
    result = PXA_STATUS_WOULD_BLOCK;
    goto out;
  }
  if (op == 4) {
    result = 1;
    goto out;
  }
  if (op == PXA_IO_READ &&
      (o->kind == PXA_LAN_DATAGRAM || o->kind == PXA_LAN_BROWSE)) {
    size_t n = packet_size(&o->stream.receive);
    if (!n) {
      result = PXA_STATUS_WOULD_BLOCK;
      goto out;
    }
    if (size < n) {
      result = PXA_STATUS_LIMIT_EXCEEDED;
      goto out;
    }
    ring_copy(&o->stream.receive, 2, data, n);
    pxa_http_ring_advance(&o->stream.receive, n + 2, 0);
    result = (int32_t)n;
    goto out;
  }
  if (op == PXA_IO_WRITE && o->kind == PXA_LAN_DATAGRAM) {
    pxa_net_endpoint_t e;
    uint32_t address;
    if (size < 24 || size > LAN_PACKET + 24 ||
        size + 2 > o->stream.send.capacity ||
        pxa_net_endpoint_decode(data, size, &e) || e.family != 4 || !e.port) {
      result = PXA_STATUS_INVALID_ARGUMENT;
      goto out;
    }
    memcpy(&address, e.address, 4);
    if ((!o->network_scope && !is_local(c, address) &&
         !(ntohl(address) == UINT32_MAX && (o->flags & PXA_LAN_BROADCAST))) ||
        ((ntohl(address) == UINT32_MAX ||
          address == (c->interface_address | ~c->interface_mask)) &&
         !(o->flags & PXA_LAN_BROADCAST))) {
      result = PXA_STATUS_DENIED;
      goto out;
    }
    result = packet_put(&o->stream.send, data, size) ? (int32_t)size
                                                     : PXA_STATUS_WOULD_BLOCK;
  }
out:
  UNLOCK(c);
  return result;
}
static pxa_status_t local_open(void *context, const pxa_lan_request_t *r,
                               void **out) {
  lan_context *c = context;
  *out = NULL;
  if (r->bind.family != 4 || r->buffer_bytes > c->config.max_buffer_bytes)
    return PXA_STATUS_UNSUPPORTED;
  LOCK(c);
  lan_object *o = object_new(c, r->kind, r->buffer_bytes);
  if (!o) {
    UNLOCK(c);
    return PXA_STATUS_RESOURCE_LIMIT;
  }
  o->local = r->bind;
  o->flags = r->flags;
  o->network_scope = r->network_scope;
  o->max_items = r->max_items;
  o->claimed = 1;
  if (r->kind >= PXA_LAN_ADVERTISE) {
    snprintf(o->discovery->type, sizeof(o->discovery->type), "%.*s.local.",
             (int)r->service_type.size, r->service_type.data);
    if (r->kind == PXA_LAN_ADVERTISE)
      snprintf(o->discovery->instance, sizeof(o->discovery->instance),
               "%.*s-%08x.%s", (int)r->instance.size, r->instance.data,
               (unsigned)next_random(c), o->discovery->type);
    o->discovery->txt_size = (uint16_t)r->txt.size;
    if (r->txt.size)
      memcpy(o->discovery->txt, r->txt.data, r->txt.size);
  }
  if (!start_worker(c)) {
    o->closed = 1;
    cleanup_objects(c);
    UNLOCK(c);
    return PXA_STATUS_RESOURCE_LIMIT;
  }
  *out = o;
  UNLOCK(c);
  return PXA_STATUS_OK;
}
static pxa_status_t local_accept_backend(void *context, void *object,
                                         void **out) {
  lan_context *c = context;
  lan_object *parent = object;
  *out = NULL;
  LOCK(c);
  pxa_status_t result = PXA_STATUS_WOULD_BLOCK;
  if (parent->kind != PXA_LAN_LISTENER)
    result = PXA_STATUS_UNSUPPORTED;
  else if (parent->error)
    result = parent->error;
  else
    for (unsigned i = 0; i < c->config.max_resources; ++i) {
      lan_object *o = c->objects[i];
      if (o && o->parent == parent && !o->claimed && !o->closed) {
        o->claimed = 1;
        *out = o;
        result = 0;
        break;
      }
    }
  UNLOCK(c);
  return result;
}
static void local_close(void *context, void *object) {
  lan_context *c = context;
  LOCK(c);
  ((lan_object *)object)->closed = 1;
  UNLOCK(c);
}
static int local_poll(void *context) {
  lan_context *c = context;
  LOCK(c);
  int changed = (int)c->changed;
  c->changed = 0;
  UNLOCK(c);
  return changed;
}
static mdns_string_t dns_string(const char *s) {
  mdns_string_t r = {s, strlen(s)};
  return r;
}
static int mdns_send(lan_object *o, int mode) {
  int goodbye = mode == 1, probe = mode == 2;
  lan_context *c = o->ctx;
  if (c->mdns_fd < 0)
    return 0;
  int shared_address = 0;
  if (goodbye) {
    for (unsigned i = 0; i < c->config.max_resources; ++i) {
      lan_object *other = c->objects[i];
      if (other && other != o && !other->closed && other->announced &&
          other->kind == PXA_LAN_ADVERTISE)
        shared_address = 1;
    }
  }
  /* Keep TXT wire bytes intact. The vendor's convenience TXT builder always
   * inserts '=' and cannot represent key-only or empty DNS-SD metadata. */
  mdns_record_t records[4] = {0};
  records[0].name = dns_string(o->discovery->type);
  records[0].type = MDNS_RECORDTYPE_PTR;
  records[0].data.ptr.name = dns_string(o->discovery->instance);
  records[1].name = dns_string(o->discovery->instance);
  records[1].type = MDNS_RECORDTYPE_SRV;
  records[1].data.srv.name = dns_string(c->hostname);
  records[1].data.srv.port = o->local.port;
  records[2].name = dns_string(c->hostname);
  records[2].type = MDNS_RECORDTYPE_A;
  records[2].data.a.addr.sin_family = AF_INET;
  records[2].data.a.addr.sin_addr.s_addr = c->interface_address;
  records[3].name = dns_string(o->discovery->instance);
  records[3].type = MDNS_RECORDTYPE_TXT;
  int requested = !goodbye && !probe && o->discovery->reply_pending;
  int unicast = requested && o->discovery->reply_pending == 1;
  unsigned selected = 0;
  if (requested) {
    if (!strcasecmp(o->discovery->reply_name, c->hostname))
      selected = 2;
    else if (!strcasecmp(o->discovery->reply_name, o->discovery->instance))
      selected = o->discovery->reply_type == MDNS_RECORDTYPE_TXT ? 3 : 1;
  }
  struct mdns_header_t *header = (struct mdns_header_t *)c->packet;
  memset(header, 0, sizeof(*header));
  header->query_id = unicast ? htons(o->discovery->reply_id) : 0;
  header->flags = probe ? 0 : htons(0x8400);
  header->questions = htons((uint16_t)(probe ? 2 : unicast));
  header->answer_rrs = probe ? 0
                             : htons((uint16_t)(requested        ? 1
                                                : shared_address ? 3
                                                                 : 4));
  header->authority_rrs = probe ? htons(3) : 0;
  header->additional_rrs = htons((uint16_t)(requested ? 3 : 0));
  mdns_string_table_t strings = {{0}, 0, 0};
  void *out = c->packet + sizeof(*header);
  if (probe) {
    out = mdns_answer_add_question_unicast(c->packet, LAN_DNS_BYTES, out,
                                           MDNS_RECORDTYPE_ANY, c->hostname,
                                           strlen(c->hostname), &strings);
    if (out)
      out = mdns_answer_add_question_unicast(
          c->packet, LAN_DNS_BYTES, out, MDNS_RECORDTYPE_ANY,
          o->discovery->instance, strlen(o->discovery->instance), &strings);
  } else if (unicast)
    out = mdns_answer_add_question_unicast(
        c->packet, LAN_DNS_BYTES, out,
        (mdns_record_type_t)o->discovery->reply_type, o->discovery->reply_name,
        strlen(o->discovery->reply_name), &strings);
  if (unicast && out)
    mdns_htons((uint8_t *)out - 2, MDNS_CLASS_IN);
  for (unsigned i = 0; out && i < 4; ++i) {
    unsigned index = i == 0 ? selected : i <= selected ? i - 1 : i;
    if ((probe && index == 0) || (shared_address && index == 2))
      continue;
    mdns_record_t record = records[index];
    int legacy = unicast && o->discovery->reply_address.sin_port != htons(5353);
    mdns_record_update_rclass_ttl(
        &record, MDNS_CLASS_IN | (legacy || probe ? 0 : MDNS_CACHE_FLUSH),
        goodbye  ? 0
        : legacy ? 10
                 : 120);
    if (record.type != MDNS_RECORDTYPE_TXT) {
      out = mdns_answer_add_record(c->packet, LAN_DNS_BYTES, out, record,
                                   &strings);
      continue;
    }
    out = mdns_answer_add_record_header(c->packet, LAN_DNS_BYTES, out, record,
                                        &strings);
    if (!out)
      break;
    size_t n = o->discovery->txt_size ? o->discovery->txt_size : 1;
    if ((uint8_t *)out + n > c->packet + LAN_DNS_BYTES)
      return 0;
    mdns_htons((uint8_t *)out - 2, (uint16_t)n);
    if (o->discovery->txt_size)
      memcpy(out, o->discovery->txt, n);
    else
      *(uint8_t *)out = 0;
    out = (uint8_t *)out + n;
  }
  int sent = 0;
  if (out) {
    size_t n = (size_t)((uint8_t *)out - c->packet);
    sent = unicast ? mdns_unicast_send(c->mdns_fd, &o->discovery->reply_address,
                                       sizeof(o->discovery->reply_address),
                                       c->packet, n)
                   : mdns_multicast_send(c->mdns_fd, c->packet, n);
    sent = sent == 0;
  }
  if (requested)
    o->discovery->reply_pending = 0;
  return sent;
}
/* Probe/claim state never adds a worker or an unbounded record cache. */
static void own_record_set(lan_object *o, int host,
                           struct pxa_mdns_record_key own[2], size_t *count) {
  memset(own, 0, sizeof(*own) * 2);
  if (host) {
    own[0].type = MDNS_RECORDTYPE_A;
    own[0].length = 4;
    memcpy(own[0].data, &o->ctx->interface_address, 4);
    *count = 1;
  } else {
    own[0].type = MDNS_RECORDTYPE_TXT;
    own[0].length = o->discovery->txt_size ? o->discovery->txt_size : 1;
    memcpy(own[0].data, o->discovery->txt, own[0].length);
    own[1].type = MDNS_RECORDTYPE_SRV;
    own[1].data[4] = (uint8_t)(o->local.port >> 8);
    own[1].data[5] = (uint8_t)o->local.port;
    size_t n = 0;
    (void)pxa_mdns_text_name(o->ctx->hostname, own[1].data + 6, &n);
    own[1].length = (uint16_t)(6 + n);
    *count = 2;
  }
}
static void check_conflict(lan_context *c, const char *name, uint16_t type,
                           uint16_t klass, uint32_t ttl,
                           mdns_entry_type_t entry, const uint8_t *data,
                           size_t size, size_t offset, size_t length) {
  if ((klass & 0x7fff) != MDNS_CLASS_IN || size < 12)
    return;
  int response = (data[2] & 0x80) != 0;
  if ((!response && entry != MDNS_ENTRYTYPE_AUTHORITY) || (response && !ttl))
    return;
  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (!o || o->closed || o->error || o->kind != PXA_LAN_ADVERTISE ||
        (!o->announced && !o->discovery->probes))
      continue;
    int host = !strcasecmp(name, c->hostname);
    if (!host && strcasecmp(name, o->discovery->instance))
      continue;
    struct pxa_mdns_record_key own[2], remote;
    size_t count;
    own_record_set(o, host, own, &count);
    if (!response) {
      if (o->announced)
        continue; /* Questions already queued its defense. */
      unsigned bit = host ? 2u : 1u;
      if (o->discovery->packet_seen & bit)
        continue;
      o->discovery->packet_seen |= (uint8_t)bit;
      if (pxa_mdns_probe_compare(data, size,
                                 host ? c->hostname : o->discovery->instance,
                                 own, count) < 0)
        o->discovery->conflict |= 16;
      continue;
    }
    if (!pxa_mdns_record_key(&remote, type, data, size, offset, length))
      continue;
    int different = 1;
    for (size_t j = 0; j < count; ++j)
      if (!pxa_mdns_key_compare(&own[j], &remote))
        different = 0;
    /* Established ownership conflicts only for our RR types. During ANY
     * probing a response for any type can show that the name is already used.
     */
    if (o->announced && type != own[0].type &&
        (count < 2 || type != own[1].type))
      different = 0;
    if (different)
      o->discovery->conflict |=
          (uint8_t)(host ? (o->announced ? 8 : 2) : (o->announced ? 4 : 1));
  }
}
static void restart_probe(lan_object *o, uint64_t next) {
  o->ready = o->announced = 0;
  o->discovery->probes = o->discovery->announcements = 0;
  o->discovery->reply_pending = o->discovery->conflict = 0;
  o->discovery->started = now_ms();
  o->discovery->next_query = next;
  o->ctx->changed = 1;
}
static void process_conflicts(lan_context *c, uint64_t now) {
  unsigned host_conflict = 0;
  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (o && !o->closed && o->kind == PXA_LAN_ADVERTISE)
      host_conflict |= o->discovery->conflict & 10u;
  }
  if (host_conflict) {
    if (host_conflict & 2)
      snprintf(c->hostname, sizeof(c->hostname), "pxa-%08x.local.",
               (unsigned)next_random(c));
    uint64_t next =
        now + (host_conflict & 2 ? 5000 : (unsigned)next_random(c) % 251);
    for (unsigned i = 0; i < c->config.max_resources; ++i) {
      lan_object *o = c->objects[i];
      if (o && !o->closed && o->kind == PXA_LAN_ADVERTISE)
        restart_probe(o, next);
    }
  }
  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (!o || o->closed || o->kind != PXA_LAN_ADVERTISE ||
        !o->discovery->conflict)
      continue;
    unsigned conflict = o->discovery->conflict;
    if (conflict & 1) {
      char base[55];
      const char *dot = strchr(o->discovery->instance, '.');
      size_t n = dot ? (size_t)(dot - o->discovery->instance) : 0;
      if (n < 9 || n - 9 >= sizeof(base)) {
        o->error = PXA_STATUS_PROTOCOL_ERROR;
        continue;
      }
      memcpy(base, o->discovery->instance, n - 9);
      base[n - 9] = 0;
      snprintf(o->discovery->instance, sizeof(o->discovery->instance),
               "%s-%08x.%s", base, (unsigned)next_random(c),
               o->discovery->type);
    }
    restart_probe(o, now + (conflict & 1    ? 5000
                            : conflict & 16 ? 1000
                                            : (unsigned)next_random(c) % 251));
  }
}
static lan_service *cache_find(lan_context *c, const char *name, int create) {
  lan_service *empty = NULL;
  for (unsigned i = 0; i < LAN_CACHE; ++i) {
    lan_service *s = &c->cache[i];
    if (s->used && !strcasecmp(s->instance, name))
      return s;
    if (!s->used && !empty)
      empty = s;
  }
  if (create && empty) {
    memset(empty, 0, sizeof(*empty));
    empty->used = 1;
    snprintf(empty->instance, sizeof(empty->instance), "%s", name);
    empty->expires = now_ms() + 10000;
    return empty;
  }
  return NULL;
}
static int type_wanted(lan_context *c, const char *name) {
  size_t n = strlen(name);
  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (o && !o->closed && o->kind == PXA_LAN_BROWSE) {
      size_t t = strlen(o->discovery->type);
      if (n > t && name[n - t - 1] == '.' &&
          !strcasecmp(name + n - t, o->discovery->type))
        return 1;
    }
  }
  return 0;
}
static void publish_cache(lan_context *c, lan_service *s, int removed) {
  if (!removed && (!s->port || !s->address || !is_local(c, s->address)))
    return;
  uint32_t hash = 2166136261u;
  const uint8_t *ip = (const uint8_t *)&s->address;
  for (unsigned i = 0; i < 4; ++i)
    hash = (hash ^ ip[i]) * 16777619u;
  hash = (hash ^ s->port) * 16777619u;
  hash = (hash ^ (unsigned)removed) * 16777619u;
  for (unsigned i = 0; i < s->txt_size; ++i)
    hash = (hash ^ s->txt[i]) * 16777619u;
  if (!s->revision || hash != s->hash) {
    s->hash = hash;
    ++s->revision;
  }
  size_t index = (size_t)(s - c->cache);

  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (!o || o->closed || o->kind != PXA_LAN_BROWSE)
      continue;
    size_t n = strlen(s->instance), t = strlen(o->discovery->type);
    if (n <= t || strcasecmp(s->instance + n - t, o->discovery->type))
      continue;
    size_t name = n - t - 1;
    if (name > 127 || o->discovery->revisions[index] == s->revision)
      continue;
    unsigned bit = 1u << index;
    if (removed && !(o->discovery->visible & bit)) {
      o->discovery->revisions[index] = s->revision;
      continue;
    }
    unsigned visible = 0;
    for (unsigned v = o->discovery->visible; v; v >>= 1)
      visible += v & 1u;
    if (!removed && !(o->discovery->visible & bit) && visible >= o->max_items)
      continue;
    uint8_t data[LAN_EVENT];
    size_t size = 32 + name + t - 7 + s->txt_size;
    if (size > sizeof(data)) {
      ++o->dropped;
      continue;
    }
    memset(data, 0, 32);
    data[0] = (uint8_t)(removed ? 3 : (o->discovery->visible & bit) ? 2 : 1);
    pxa_write_u16(data + 2, (uint16_t)name);
    pxa_write_u16(data + 4, (uint16_t)(t - 7));
    pxa_write_u16(data + 6, s->txt_size);
    pxa_net_endpoint_t e = {0};
    e.family = 4;
    e.port = s->port;
    memcpy(e.address, &s->address, 4);
    pxa_net_endpoint_encode(data + 8, &e);
    memcpy(data + 32, s->instance, name);
    memcpy(data + 32 + name, o->discovery->type, t - 7);
    memcpy(data + 32 + name + t - 7, s->txt, s->txt_size);
    if (!packet_put(&o->stream.receive, data, size))
      ++o->dropped;
    else {
      c->changed = 1;
      o->discovery->revisions[index] = s->revision;
      if (removed)
        o->discovery->visible &= (uint8_t)~bit;
      else
        o->discovery->visible |= (uint8_t)bit;
    }
  }
  s->complete = !removed;
}
static int mdns_record(int sock, const struct sockaddr *from, size_t fromlen,
                       mdns_entry_type_t entry, uint16_t query, uint16_t type,
                       uint16_t klass, uint32_t ttl, const void *data,
                       size_t size, size_t name_offset, size_t name_length,
                       size_t record_offset, size_t record_length,
                       void *context) {
  (void)sock;
  (void)fromlen;
  (void)query;
  (void)klass;
  (void)name_length;
  lan_context *c = context;
  if (from->sa_family != AF_INET ||
      !is_local(c, ((const struct sockaddr_in *)from)->sin_addr.s_addr))
    return 0;
  char name[160];
  size_t at = name_offset;
  mdns_string_t n =
      mdns_string_extract(data, size, &at, name, sizeof(name) - 1);
  name[n.length] = 0;
  if (entry == MDNS_ENTRYTYPE_QUESTION) {
    /* Legacy/QU responses may repeat a Question Section. It is not another
     * query: answering it creates unnecessary response feedback traffic. */
    if (size < 12 || (((const uint8_t *)data)[2] & 0x80) ||
        (klass & 0x7fff) != MDNS_CLASS_IN)
      return 0;
    for (unsigned i = 0; i < c->config.max_resources; ++i) {
      lan_object *o = c->objects[i];
      if (!o || o->closed || !o->announced || o->kind != PXA_LAN_ADVERTISE)
        continue;
      if ((!strcasecmp(name, o->discovery->type) &&
           (type == MDNS_RECORDTYPE_PTR || type == MDNS_RECORDTYPE_ANY)) ||
          (!strcasecmp(name, o->discovery->instance) &&
           (type == MDNS_RECORDTYPE_SRV || type == MDNS_RECORDTYPE_TXT ||
            type == MDNS_RECORDTYPE_ANY)) ||
          (!strcasecmp(name, c->hostname) &&
           (type == MDNS_RECORDTYPE_A || type == MDNS_RECORDTYPE_ANY))) {
        o->discovery->reply_address = *(const struct sockaddr_in *)from;
        snprintf(o->discovery->reply_name, sizeof(o->discovery->reply_name),
                 "%s", name);
        o->discovery->reply_id = query;
        o->discovery->reply_type = type;
        if ((klass & MDNS_UNICAST_RESPONSE) ||
            ((const struct sockaddr_in *)from)->sin_port != htons(5353))
          o->discovery->reply_pending = 1;
        else
          o->discovery->reply_pending = 2;
      }
    }
    return 0;
  }
  check_conflict(c, name, type, klass, ttl, entry, data, size, record_offset,
                 record_length);
  /* Probe Authority and Known Answers are tentative, never discovered services.
   */
  if (!(((const uint8_t *)data)[2] & 0x80))
    return 0;
  lan_service *s = NULL;
  uint64_t expires = now_ms() + (uint64_t)(ttl > 120 ? 120 : ttl) * 1000;
  if (type == MDNS_RECORDTYPE_PTR) {
    char instance[160];
    mdns_string_t p =
        mdns_record_parse_ptr(data, size, record_offset, record_length,
                              instance, sizeof(instance) - 1);
    instance[p.length] = 0;
    if (type_wanted(c, instance))
      s = cache_find(c, instance, 1);
  } else if (type == MDNS_RECORDTYPE_SRV && type_wanted(c, name)) {
    s = cache_find(c, name, 1);
    if (s) {
      char host[160];
      mdns_record_srv_t r = mdns_record_parse_srv(
          data, size, record_offset, record_length, host, sizeof(host) - 1);
      host[r.name.length] = 0;
      s->port = r.port;
      snprintf(s->host, sizeof(s->host), "%s", host);
    }
  } else if (type == MDNS_RECORDTYPE_TXT && type_wanted(c, name)) {
    s = cache_find(c, name, 1);
    if (s && record_length <= 256 && record_offset <= size &&
        record_length <= size - record_offset) {
      s->txt_size = (uint16_t)record_length;
      memcpy(s->txt, (const uint8_t *)data + record_offset, record_length);
    }
  } else if (type == MDNS_RECORDTYPE_A) {
    struct sockaddr_in a;
    if (mdns_record_parse_a(data, size, record_offset, record_length, &a))
      for (unsigned i = 0; i < LAN_CACHE; ++i)
        if (c->cache[i].used && !strcasecmp(c->cache[i].host, name)) {
          c->cache[i].address = a.sin_addr.s_addr;
          if (!ttl)
            c->cache[i].expires = now_ms();
          if (c->cache[i].port)
            publish_cache(c, &c->cache[i], !ttl);
        }
  }
  if (s) {
    s->expires = expires;
    if (!ttl) {
      if (s->revision)
        publish_cache(c, s, 1);
    } else if (s->port && s->address && is_local(c, s->address))
      publish_cache(c, s, 0);
  }
  return 0;
}
static void mdns_step(lan_context *c, uint64_t now) {
  if (!c->cache)
    return;
  int used = 0;
  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (o && !o->closed && !o->error && o->kind >= PXA_LAN_ADVERTISE &&
        o->kind <= PXA_LAN_BROWSE)
      used = 1;
  }
  if (!used) {
    if (c->mdns_fd >= 0) {
      mdns_socket_close(c->mdns_fd);
      c->mdns_fd = -1;
    }
    free(c->cache);
    c->cache = NULL;
    free(c->packet);
    c->packet = NULL;
    return;
  }
  /* Consume live conflicts before claiming success at the end of a probe
   * window. */
  for (unsigned i = 0; i < 4 && c->mdns_fd >= 0 && readable_fd(c->mdns_fd);
       ++i) {
    for (unsigned j = 0; j < c->config.max_resources; ++j)
      if (c->objects[j] && c->objects[j]->discovery)
        c->objects[j]->discovery->packet_seen = 0;
    (void)mdns_socket_listen(c->mdns_fd, c->packet, LAN_DNS_BYTES, mdns_record,
                             c);
  }
  process_conflicts(c, now);
  for (unsigned i = 0; i < c->config.max_resources; ++i) {
    lan_object *o = c->objects[i];
    if (!o || o->closed || o->error || o->kind < PXA_LAN_ADVERTISE ||
        o->kind > PXA_LAN_BROWSE)
      continue;
    lan_discovery *d = o->discovery;
    if (o->kind == PXA_LAN_ADVERTISE) {
      if (!d->started) {
        d->started = now;
        d->next_query = now + (unsigned)next_random(c) % 251;
      }
      memcpy(o->local.address, &c->interface_address, 4);
      if (now >= d->next_query) {
        if (!o->announced && d->probes < 3) {
          if (mdns_send(o, 2))
            ++d->probes;
          d->next_query = now + 250;
        } else if (!o->announced) {
          if (!mdns_send(o, 0)) {
            d->next_query = now + 250;
            continue;
          }
          o->ready = o->announced = 1;
          d->announcements = 1;
          d->next_query = now + 1000;
          c->changed = 1;
        } else if (d->announcements == 1) {
          if (!mdns_send(o, 0)) {
            d->next_query = now + 250;
            continue;
          }
          d->announcements = 2;
          d->next_query = UINT64_MAX;
        }
      }
      if (o->announced && d->reply_pending)
        mdns_send(o, 0);
    } else if (o->ready && now >= d->next_query) {
      (void)mdns_query_send(c->mdns_fd, MDNS_RECORDTYPE_PTR, d->type,
                            strlen(d->type), c->packet, LAN_DNS_BYTES, 0);
      d->query_interval =
          d->query_interval
              ? (d->query_interval < 30000 ? d->query_interval * 2 : 60000)
              : 2000;
      d->next_query = now + d->query_interval;
    }
  }
  for (unsigned i = 0; i < LAN_CACHE; ++i) {
    lan_service *s = &c->cache[i];
    if (!s->used)
      continue;
    if (now >= s->expires) {
      if (s->complete || s->revision)
        publish_cache(c, s, 1);
      int pending = 0;
      for (unsigned j = 0; j < c->config.max_resources; ++j) {
        lan_object *o = c->objects[j];
        if (o && o->kind == PXA_LAN_BROWSE && o->discovery->revisions[i] &&
            o->discovery->revisions[i] != s->revision)
          pending = 1;
      }
      if (!pending) {
        for (unsigned j = 0; j < c->config.max_resources; ++j)
          if (c->objects[j] && c->objects[j]->discovery)
            c->objects[j]->discovery->revisions[i] = 0;
        memset(s, 0, sizeof(*s));
      }
      continue;
    }
    if (s->port && s->address && is_local(c, s->address))
      publish_cache(c, s, 0);
    if (!s->complete && now >= s->next_resolve) {
      const char *target = s->port ? s->host : s->instance;
      (void)mdns_query_send(
          c->mdns_fd, s->port ? MDNS_RECORDTYPE_A : MDNS_RECORDTYPE_SRV, target,
          strlen(target), c->packet, LAN_DNS_BYTES, 0);
      s->next_resolve = now + 2000;
    }
  }
}
int pxa_lan_native_create(const pxa_lan_native_config_t *config,
                          pxa_lan_backend_t *backend) {
  if (!config || !backend || !config->interface || !config->notify ||
      !config->max_resources || config->max_resources > LAN_MAX_OBJECTS ||
      config->max_buffer_bytes < 512 || config->max_buffer_bytes > 16384)
    return 0;
  lan_context *c = lan_alloc(sizeof(*c));
  if (!c)
    return 0;
  c->config = *config;
  c->mdns_fd = -1;
  c->random_state = (uint32_t)(now_ms() ^ (uintptr_t)c ^ UINT32_C(0x9e3779b9));
  snprintf(c->hostname, sizeof(c->hostname), "pxa-%08x.local.",
           (unsigned)next_random(c));
#ifdef ESP_PLATFORM
  c->mutex = xSemaphoreCreateMutex();
  if (!c->mutex) {
    free(c);
    return 0;
  }
#else
  if (pthread_mutex_init(&c->mutex, NULL)) {
    free(c);
    return 0;
  }
#endif
  *backend = (pxa_lan_backend_t){
      sizeof(*backend),
      PXA_NET_FEATURE_LISTEN | PXA_NET_FEATURE_DATAGRAM |
          PXA_NET_FEATURE_DISCOVERY | PXA_NET_FEATURE_LOCAL_IPV4,
      c,
      local_open,
      local_io,
      local_accept_backend,
      local_close,
      local_poll,
      config->max_resources,
      config->max_buffer_bytes};
  return 1;
}
void pxa_lan_native_destroy(pxa_lan_backend_t *backend) {
  if (!backend || !backend->context)
    return;
  lan_context *c = backend->context;
  LOCK(c);
  c->stopping = 1;
  UNLOCK(c);
#ifdef ESP_PLATFORM
  for (;;) {
    LOCK(c);
    unsigned running = c->running;
    UNLOCK(c);
    if (!running)
      break;
    vTaskDelay(1);
  }
#else
  if (c->worker_valid)
    pthread_join(c->worker, NULL);
#endif
  LOCK(c);
  for (unsigned i = 0; i < c->config.max_resources; ++i)
    if (c->objects[i])
      c->objects[i]->closed = 1;
  for (unsigned i = 0; i < LAN_MAX_OBJECTS; ++i)
    cleanup_objects(c);
  if (c->mdns_fd >= 0)
    mdns_socket_close(c->mdns_fd);
  UNLOCK(c);
#ifdef ESP_PLATFORM
  vSemaphoreDelete(c->mutex);
#else
  pthread_mutex_destroy(&c->mutex);
#endif
  free(c->cache);
  free(c->packet);
  free(c);
  memset(backend, 0, sizeof(*backend));
}
