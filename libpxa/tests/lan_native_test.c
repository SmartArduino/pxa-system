/* SPDX-License-Identifier: MIT */
#define _DEFAULT_SOURCE 1
#include "../adapters/network/mdns_probe.h"
#include "../adapters/network/pxa_lan_native.h"
#include <arpa/inet.h>
#include <assert.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static void pause_step(void) {
  struct timespec d = {0, 10000000};
  nanosleep(&d, NULL);
}
static int interface(void *p, uint32_t *ip, uint32_t *mask) {
  (void)p;
  struct ifaddrs *list = NULL;
  if (getifaddrs(&list))
    return 0;
  int found = 0;
  for (struct ifaddrs *i = list; i; i = i->ifa_next)
    if (i->ifa_addr && i->ifa_netmask && i->ifa_addr->sa_family == AF_INET &&
        (i->ifa_flags & IFF_UP)) {
      *ip = ((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr;
      *mask = ((struct sockaddr_in *)i->ifa_netmask)->sin_addr.s_addr;
      found = 1;
      if (!(i->ifa_flags & IFF_LOOPBACK))
        break;
    }
  freeifaddrs(list);
  return found;
}
static void notify(void *p) { (void)p; }
static pxa_net_endpoint_t endpoint(pxa_lan_backend_t *b, void *o) {
  uint8_t data[24];
  int32_t n = PXA_STATUS_WOULD_BLOCK;
  for (unsigned i = 0; i < 1200 && n == PXA_STATUS_WOULD_BLOCK; ++i) {
    n = b->io(b->context, o, 5, data, sizeof(data));
    if (n < 0)
      pause_step();
  }
  assert(n == 24);
  pxa_net_endpoint_t e;
  assert(!pxa_net_endpoint_decode(data, 24, &e));
  return e;
}
static void *open_object(pxa_lan_backend_t *b, unsigned kind, unsigned port) {
  pxa_lan_request_t r = {0};
  r.kind = (uint16_t)kind;
  r.bind.family = 4;
  r.bind.port = (uint16_t)port;
  r.buffer_bytes = 4096;
  r.max_items = 2;
  r.flags = kind == 1 ? 1 : 0;
  void *o = NULL;
  assert(!b->open(b->context, &r, &o));
  assert(o);
  (void)endpoint(b, o);
  return o;
}
static struct sockaddr_in addr(pxa_net_endpoint_t e) {
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_port = htons(e.port);
  memcpy(&a.sin_addr, e.address, 4);
  return a;
}
static int32_t read_wait(pxa_lan_backend_t *b, void *o, uint8_t *p,
                         size_t size) {
  int32_t n = PXA_STATUS_WOULD_BLOCK;
  for (unsigned i = 0; i < 500 && n == PXA_STATUS_WOULD_BLOCK; ++i) {
    n = b->io(b->context, o, 1, p, size);
    if (n < 0)
      pause_step();
  }
  return n;
}
static void probe_codec_test(void) {
  uint8_t packet[512] = {0}, name[256];
  size_t name_size;
  assert(pxa_mdns_text_name("test.local.", name, &name_size));
  packet[9] = 2; /* two Authority records, SRV first to test set sorting */
  size_t at = 12;
  memcpy(packet + at, name, name_size);
  at += name_size;
  uint8_t srv_header[] = {0, 33, 0, 1, 0, 0, 0, 120, 0, 8};
  memcpy(packet + at, srv_header, sizeof(srv_header));
  at += sizeof(srv_header);
  memset(packet + at, 0, 8);
  packet[at + 5] = 100;
  packet[at + 6] = 0xc0;
  packet[at + 7] = 12;
  at += 8;
  packet[at++] = 0xc0;
  packet[at++] = 12;
  uint8_t txt_record[] = {0, 16, 0, 1, 0, 0, 0, 120, 0, 4, 3, 'v', '=', '2'};
  memcpy(packet + at, txt_record, sizeof(txt_record));
  at += sizeof(txt_record);
  struct pxa_mdns_record_key own[2] = {0};
  own[0].type = 16;
  own[0].length = 4;
  memcpy(own[0].data, "\3v=1", 4);
  own[1].type = 33;
  own[1].data[5] = 200;
  memcpy(own[1].data + 6, name, name_size);
  own[1].length = (uint16_t)(6 + name_size);
  assert(pxa_mdns_probe_compare(packet, at, "test.local.", own, 2) < 0);
  own[0].data[3] = '3';
  assert(pxa_mdns_probe_compare(packet, at, "TEST.local.", own, 2) > 0);
  own[0].data[3] = '2';
  assert(pxa_mdns_probe_compare(packet, at, "test.local.", own, 2) > 0);
  own[1].data[5] = 100;
  assert(!pxa_mdns_probe_compare(packet, at, "test.local.", own, 2));
  /* Compression cycles and every truncated packet must terminate safely. */
  for (size_t n = 0; n < at; ++n)
    (void)pxa_mdns_probe_compare(packet, n, "test.local.", own, 2);
  packet[12] = 0xc0;
  packet[13] = 12;
  assert(!pxa_mdns_probe_compare(packet, at, "test.local.", own, 2));
  uint32_t seed = 29;
  for (unsigned i = 0; i < 10000; ++i) {
    seed = seed * 1664525u + 1013904223u;
    packet[(seed >> 16) % sizeof(packet)] = (uint8_t)seed;
    (void)pxa_mdns_probe_compare(packet, seed % sizeof(packet), "test.local.",
                                 own, 2);
  }
}
static int loop_interface(void *context, uint32_t *ip, uint32_t *mask) {
  (void)context;
  *ip = htonl(UINT32_C(0x7f000001));
  *mask = UINT32_MAX;
  return 1;
}
static void network_scope_test(void) {
  uint32_t ip, mask;
  assert(interface(NULL, &ip, &mask));
  if ((ntohl(ip) >> 24) == 127)
    return; /* No non-loopback peer in this host. */
  int peer = socket(AF_INET, SOCK_DGRAM, 0);
  assert(peer >= 0);
  struct sockaddr_in peer_addr = {0};
  peer_addr.sin_family = AF_INET;
  peer_addr.sin_addr.s_addr = ip;
  assert(!bind(peer, (struct sockaddr *)&peer_addr, sizeof(peer_addr)));
  socklen_t peer_size = sizeof(peer_addr);
  assert(!getsockname(peer, (struct sockaddr *)&peer_addr, &peer_size));
  struct timeval timeout = {2, 0};
  assert(!setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
  pxa_lan_native_config_t config = {NULL, loop_interface, notify, 8, 4096};
  pxa_lan_backend_t backend;
  assert(pxa_lan_native_create(&config, &backend));
  pxa_lan_request_t request = {0};
  request.kind = PXA_LAN_DATAGRAM;
  request.bind.family = 4;
  request.buffer_bytes = 4096;
  request.max_items = 2;
  void *local, *network;
  assert(!backend.open(backend.context, &request, &local));
  (void)endpoint(&backend, local);
  request.network_scope = 1;
  assert(!backend.open(backend.context, &request, &network));
  (void)endpoint(&backend, network);
  uint8_t packet[128];
  pxa_net_endpoint_t target = {0};
  target.family = 4;
  target.port = ntohs(peer_addr.sin_port);
  memcpy(target.address, &ip, 4);
  pxa_net_endpoint_encode(packet, &target);
  memcpy(packet + 24, "outside", 7);
  assert(backend.io(backend.context, local, 2, packet, 31) ==
         PXA_STATUS_DENIED);
  assert(backend.io(backend.context, network, 2, packet, 31) == 31);
  struct sockaddr_in from;
  socklen_t from_size = sizeof(from);
  assert(recvfrom(peer, packet, sizeof(packet), 0, (struct sockaddr *)&from,
                  &from_size) == 7);
  assert(!memcmp(packet, "outside", 7));
  assert(sendto(peer, "reply", 5, 0, (struct sockaddr *)&from, from_size) == 5);
  assert(read_wait(&backend, network, packet, sizeof(packet)) == 29 &&
         !memcmp(packet + 24, "reply", 5));
  backend.close(backend.context, local);
  backend.close(backend.context, network);
  pxa_lan_native_destroy(&backend);
  close(peer);
}
static int wire_advertise(int probe_mode) {
  pxa_lan_native_config_t config = {NULL, interface, notify, 12, 8192};
  pxa_lan_backend_t b;
  assert(pxa_lan_native_create(&config, &b));
  void *listener = open_object(&b, PXA_LAN_LISTENER, 0);
  pxa_net_endpoint_t e = endpoint(&b, listener);
  pxa_lan_request_t r = {0};
  r.kind = PXA_LAN_ADVERTISE;
  r.bind = e;
  r.buffer_bytes = 4096;
  r.max_items = 2;
  r.service_type = (pxa_bytes_t){(const uint8_t *)"_pxawire._tcp", 13};
  r.instance = (pxa_bytes_t){(const uint8_t *)"wire-meta", 9};
  uint8_t txt[] = {3,   'v', '=', '1', 4,   'f', 'l', 'a',
                   'g', 5,   'b', 'i', 'n', '=', 255};
  r.txt = (pxa_bytes_t){txt, sizeof(txt)};
  if (probe_mode) {
    printf("OPEN %u.%u.%u.%u %u\n", e.address[0], e.address[1], e.address[2],
           e.address[3], e.port);
    fflush(stdout);
  }
  void *advert = NULL;
  assert(!b.open(b.context, &r, &advert));
  (void)endpoint(&b, advert);
  if (probe_mode) {
    puts("READY");
    fflush(stdout);
    for (unsigned i = 0; i < 1000; ++i)
      pause_step();
    b.close(b.context, advert);
    b.close(b.context, listener);
    pxa_lan_native_destroy(&b);
    return 0;
  }
  r.instance = (pxa_bytes_t){(const uint8_t *)"wire-empty", 10};
  r.txt = (pxa_bytes_t){NULL, 0};
  void *empty = NULL;
  assert(!b.open(b.context, &r, &empty));
  (void)endpoint(&b, empty);
  printf("%u.%u.%u.%u %u\n", e.address[0], e.address[1], e.address[2],
         e.address[3], e.port);
  fflush(stdout);
  for (unsigned i = 0; i < 1000; ++i)
    pause_step();
  b.close(b.context, advert);
  b.close(b.context, empty);
  b.close(b.context, listener);
  pxa_lan_native_destroy(&b);
  return 0;
}
int main(int argc, char **argv) {
  probe_codec_test();
  network_scope_test();
  if (argc == 2 && !strcmp(argv[1], "--advertise"))
    return wire_advertise(0);
  if (argc == 2 && !strcmp(argv[1], "--probe-advertise"))
    return wire_advertise(1);
  pxa_lan_native_config_t config = {NULL, interface, notify, 12, 8192};
  pxa_lan_backend_t b;
  assert(pxa_lan_native_create(&config, &b));
  for (unsigned round = 0; round < 3; ++round) {
    void *listener = open_object(&b, PXA_LAN_LISTENER, 0);
    pxa_net_endpoint_t e = endpoint(&b, listener);
    assert(e.port);
    int client = socket(AF_INET, SOCK_STREAM, 0);
    assert(client >= 0);
    struct sockaddr_in a = addr(e);
    assert(!connect(client, (struct sockaddr *)&a, sizeof(a)));
    void *peer = NULL;
    for (unsigned i = 0; i < 300 && !peer; ++i) {
      assert(b.accept(b.context, listener, &peer) ==
             (peer ? 0 : PXA_STATUS_WOULD_BLOCK));
      if (!peer)
        pause_step();
    }
    assert(peer);
    b.close(b.context, listener); /* Accepted streams survive listener close. */
    uint8_t data[2048];
    assert(send(client, "hello", 5, 0) == 5);
    assert(read_wait(&b, peer, data, sizeof(data)) == 5 &&
           !memcmp(data, "hello", 5));
    assert(b.io(b.context, peer, 2, (uint8_t *)"world", 5) == 5);
    struct timeval timeout = {2, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    assert(recv(client, data, 5, MSG_WAITALL) == 5 &&
           !memcmp(data, "world", 5));
    close(client);
    int done = 0;
    for (unsigned i = 0; i < 300; ++i) {
      int n = b.io(b.context, peer, PXA_IO_POLL, NULL, 0);
      if (n & PXA_READY_END) {
        done = 1;
        break;
      }
      pause_step();
    }
    assert(done && b.io(b.context, peer, 1, data, sizeof(data)) == 0);
    b.close(b.context, peer);
    for (unsigned i = 0; i < 5; ++i)
      pause_step();
  }
  void *udp = open_object(&b, PXA_LAN_DATAGRAM, 0);
  pxa_net_endpoint_t e = endpoint(&b, udp);
  struct sockaddr_in a = addr(e);
  int client = socket(AF_INET, SOCK_DGRAM, 0);
  assert(client >= 0);
  uint8_t data[2048];
  assert(sendto(client, "abc", 3, 0, (struct sockaddr *)&a, sizeof(a)) == 3);
  assert(read_wait(&b, udp, data, 1) == PXA_STATUS_LIMIT_EXCEEDED);
  assert(read_wait(&b, udp, data, sizeof(data)) == 27 &&
         !memcmp(data + 24, "abc", 3));
  pxa_net_endpoint_t sender;
  assert(!pxa_net_endpoint_decode(data, 24, &sender));
  memcpy(data + 24, "reply", 5);
  assert(b.io(b.context, udp, 2, data, 29) == 29);
  assert(recv(client, data, sizeof(data), 0) == 5 && !memcmp(data, "reply", 5));
  memset(data, 1, 1500);
  sendto(client, data, 1500, 0, (struct sockaddr *)&a, sizeof(a));
  for (unsigned i = 0; i < 10; ++i)
    pause_step();
  assert(b.io(b.context, udp, 8, data, 32) == 32 &&
         pxa_read_u32(data + 16) > 0);
  pxa_net_endpoint_t outside = {4, 90, 0, {192, 0, 2, 1}};
  pxa_net_endpoint_encode(data, &outside);
  assert(b.io(b.context, udp, 2, data, 24) == PXA_STATUS_DENIED);
  close(client);
  b.close(b.context, udp);
  /* Two independent Host backends discover each other via real multicast. */
  pxa_lan_backend_t second;
  assert(pxa_lan_native_create(&config, &second));
  pxa_lan_request_t r = {0};
  r.kind = PXA_LAN_BROWSE;
  r.bind.family = 4;
  r.buffer_bytes = 4096;
  r.max_items = 4;
  r.service_type = (pxa_bytes_t){(const uint8_t *)"_pxatest._tcp", 13};
  void *browser = NULL;
  assert(!second.open(second.context, &r, &browser));
  (void)endpoint(&second, browser);
  void *listener = open_object(&b, PXA_LAN_LISTENER, 0);
  e = endpoint(&b, listener);
  r.kind = PXA_LAN_ADVERTISE;
  r.bind = e;
  r.instance = (pxa_bytes_t){(const uint8_t *)"native-test", 11};
  uint8_t txt[] = {3, 'v', '=', '1'};
  r.txt = (pxa_bytes_t){txt, 4};
  void *advert = NULL;
  assert(!b.open(b.context, &r, &advert));
  (void)endpoint(&b, advert);
  int32_t n = read_wait(&second, browser, data, sizeof(data));
  assert(n > 32 && data[0] == 1 && pxa_read_u16(data + 10) == e.port);
  r.instance = (pxa_bytes_t){(const uint8_t *)"native-sibling", 14};
  void *sibling = NULL;
  assert(!b.open(b.context, &r, &sibling));
  (void)endpoint(&b, sibling);
  int sibling_added = 0;
  for (unsigned i = 0; i < 500 && !sibling_added; ++i) {
    n = second.io(second.context, browser, 1, data, sizeof(data));
    if (n > 46 && data[0] == 1 && !memcmp(data + 32, "native-sibling", 14))
      sibling_added = 1;
    else
      pause_step();
  }
  assert(sibling_added);
  b.close(b.context, advert);
  int removed = 0;
  for (unsigned i = 0; i < 500 && !removed; ++i) {
    n = second.io(second.context, browser, 1, data, sizeof(data));
    if (n > 44 && data[0] == 3) {
      assert(!memcmp(data + 32, "native-test-", 12));
      removed = 1;
    } else
      pause_step();
  }
  assert(removed);
  /* All zero-TTL records in one goodbye describe removal, never re-addition. */
  for (unsigned i = 0; i < 10; ++i) {
    pause_step();
    n = second.io(second.context, browser, 1, data, sizeof(data));
    assert(n == PXA_STATUS_WOULD_BLOCK ||
           (n > 44 && data[0] == 3 && !memcmp(data + 32, "native-test-", 12)));
  }
  b.close(b.context, sibling);
  b.close(b.context, listener);
  second.close(second.context, browser);
  pxa_lan_native_destroy(&second);
  pxa_lan_native_destroy(&b);
  puts("LAN native PASS: TCP accept/EOF/restart, UDP "
       "boundaries/overflow/policy, standard mDNS discovery/goodbye");
}
