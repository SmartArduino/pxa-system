# PXA Network Draft 0.7

Network service 0.7.0 provides bounded, Host-managed HTTP(S) requests. Opcode 1 is
the frozen v1.0 GET operation. Opcode 2 adds GET, HEAD, POST, PUT, PATCH and
DELETE, bounded request headers and inline bodies, per-request timeouts,
selected response headers, response length metadata and optional response
streams. Version 0.3 adds the explicitly granted `web` authority below. Version
0.4 adds bounded bidirectional exchanges and response heads before the body
completes. A 0.4 Host continues accepting opcodes 1 and 2 unchanged.

## Security and URL model

Version 0.3 additionally accepts the exact reserved scope `web` for
`net.client`. The signed manifest must declare that scope and Host policy must
grant it; a handle acquired for `web` permits any validated HTTP(S) URL. It is
intended for clients that let users add servers, such as readers. It is not a
wildcard Permission matcher: every other service and origin remains exact.
An origin-bound handle cannot become a `web` handle. Missing, wrong-service,
stale or revoked handles remain rejected, including under the 64-bit Core v1
binding. Redirects still require a new explicit request.

Every request carries a live `net.client` Permission Handle. Its scope must
equal the parsed URL origin byte for byte, for example `https://example.test`
or `http://8.166.128.230:3100`. The origin includes an explicit port, including
`:443`; it never includes the path or query. Permission revocation cancels
pending operations and closes response streams through normal authority-bound
Handle revocation.

URLs are 1 to 512 bytes and start with lowercase `https://` or `http://`. The
host is a lowercase DNS name with labels of at most 63 bytes and total length
at most 253 bytes; HTTP URLs also accept IPv4 literals for product endpoints.
An optional decimal port is in the range 1..65535. A path or query may follow
the authority. URLs reject credentials, fragments, backslashes, spaces,
control bytes, IPv6 literals and malformed percent escapes. No redirect is
followed automatically, because a redirect target may have different
permission authority.

TLS roots, certificate and hostname verification, DNS, proxy configuration,
Wi-Fi credentials and transport selection remain Host policy. Guest code never
receives sockets, credentials or native transport objects.

## Asynchronous contract

The Guest submits a reliable request with a nonzero request ID. The import only
validates and queues work: DNS, TLS and network I/O must not execute in the
Guest import callback. The Host backend starts work asynchronously and
`pxa_net_poll()` completes it later. Component stop, request cancellation,
permission revocation and timeout all cancel the backend operation.
The Host reserves enough Core event storage for the bounded response before
starting the backend. If the reservation cannot be made, the control import
returns `resource-limit` and no network operation starts. Once accepted, the
completion remains reliable even when the Guest mailbox is temporarily full.

A successful network operation reports an HTTP status from 100 through 599;
HTTP error statuses such as 404 or 500 are still ABI success. Transport and
policy failures use Core statuses. In particular, timeout is `timed-out`, loss
of a usable transport is `unavailable`, malformed provider output is
`protocol-error`, and a response exceeding an agreed bound is `limit-exceeded`.

## Opcode 1: FETCH (v1.0 compatibility)

The request contains exactly one each of URL (tag 1), GET method value (tag 2),
Permission Handle (tag 3) and nonzero maximum response bytes (tag 4). It uses
the Host default timeout of 15000 ms. The success result contains status code
(tag 5), ASCII content type (tag 6) and a body Stream Handle (tag 7). Even an
empty legacy response owns a Stream Handle. The requested maximum must not
exceed the Host response cap.

## Opcode 2: HTTP_REQUEST (v1.1)

The request records are in ascending tag order:

| Tag | Field | Cardinality and rule |
| --- | --- | --- |
| 1 | URL | exactly one HTTP(S) URL |
| 2 | method | exactly one u16: GET=1, HEAD=2, POST=3, PUT=4, PATCH=5, DELETE=6 |
| 3 | Permission Handle | exactly one nonzero u32 |
| 4 | maximum response bytes | exactly one nonzero u32, at most 262144 |
| 8 | timeout milliseconds | exactly one u32 in 100..60000 |
| 9 | request header | zero to 8 nested name/value records |
| 10 | inline request body | zero or one, at most 2048 bytes |
| 11 | wanted response header | zero to 8 unique header names |

GET and HEAD have no request body. Legacy SDKs emit lowercase HTTP token names;
Hosts also accept uppercase tokens and compare names case-insensitively. Names are
1 to 64 bytes. Values are at most 256 printable ASCII bytes, with horizontal
tab also allowed. The total encoded request-header block is at most 2048 bytes.
Guest-controlled `connection`, `content-length`, `host`, `proxy-connection`,
`te`, `trailer`, `transfer-encoding`, `upgrade` and `expect` are forbidden. The Host owns
framing, authority and hop-by-hop behavior.

Wanted response headers form an allowlist. The provider returns only available
headers named by that list, preserving duplicate fields in provider order. This avoids exposing ambient
or sensitive headers the Guest did not request.

The success result records are also in ascending tag order:

| Tag | Field | Cardinality and rule |
| --- | --- | --- |
| 5 | status code | exactly one u16 in 100..599 |
| 6 | content type | exactly one printable ASCII value, at most 96 bytes |
| 7 | body Stream Handle | present exactly when BODY_PRESENT is set |
| 9 | selected response header | zero to 8 nested name/value records |
| 12 | body length | u64 present exactly when BODY_LENGTH_KNOWN is set |
| 13 | response flags | exactly one u32 |

Response flag bit 0 is BODY_PRESENT and bit 1 is BODY_LENGTH_KNOWN. Unknown bits
are invalid. A HEAD or empty response can omit BODY_PRESENT and therefore has no
Stream Handle. If no body is present, a known length must be zero. If a known
length exceeds the request maximum, completion is `limit-exceeded`.

The body Stream is Component-owned, read-only and authority-bound. The Guest
reads it with `pxa_io(handle, read, ...)`; each call is capped by the request's
maximum remaining bytes. Providers may return `would-block` while streaming.
The Guest closes the Handle when finished. Host cleanup also closes it on
Component stop, permission revocation or failed result delivery.

## Core 1.0 binding

The Net service opcodes and record tags remain the same under `pxa.core.v1`.
The v1 envelope carries a nonzero 64-bit request token. Request tag 3 is a
native 64-bit Permission Handle, and response tag 7 is a native 64-bit Stream
Handle. Both have a nonzero high 32-bit generation; the Host rejects the old
four-byte tag in a Core v1 Component. All other fields retain their widths.
Both FETCH and HTTP_REQUEST complete asynchronously, and the Stream is read
with the v1 `pxa_io(handle:u64, ...)` import and closed with Core v1
`close-handle`. Cancelling the request token before completion cancels the
backend operation. The Host validates the live full-width Permission Handle
again when polling and revokes dependent streams with its authority.

`pxa_net.h` builds either request directly in one caller-owned packet and
parses completion records into borrowed views. The packet can be up to the
Core 4096-byte control limit; the Host adapter copies at most 4076 payload
bytes into one engine-owned scratch buffer before service dispatch. The
Net backend must snapshot all request fields it needs after `start()` returns.

## Resource limits

The portable profile allows at most two pending requests per Component, eight
request or selected response headers, 2048 encoded header bytes, a 2048-byte
inline request body and a 262144-byte response. Hosts may configure smaller
limits and report `resource-limit`, `invalid-argument` or `limit-exceeded` as
appropriate. Core uses fixed Host-provided workspaces and bounded slot tables;
the ABI does not require allocation on the request path.

UDP, listening sockets, multipart construction, cookie jars,
automatic decompression, redirects and streaming request bodies are outside
Network 0.2.

## Version 0.4: streaming exchanges

HTTP_OPEN (3) returns an owned exchange immediately. CAPABILITIES (4) queries backend support and current bounds. HTTP_OPEN keeps ordered request records but uses u64 tag 4 for total response budget; tag 14/u32 is the combined receive/upload buffer budget, tag 15/u64 is known upload size, and tag 16/u32 is the total response-head budget. These three fields are required. Core wire profile 1 (64-bit handles, used by the current Core v2 import) is required. Request bodies can be captured inline or written incrementally, but their declared length must agree.

The exchange uses IO READ=1, WRITE=2, POLL=3, RESPONSE=4, RESPONSE_SIZE=5, FINISH_UPLOAD=6. READ zero means successful EOF; would-block is temporary. Buffered data drains before a terminal error. RESPONSE returns a 16-byte prefix (status/u16, flags/u16, header byte count/u32, body length/u64) followed by repeated name-size/u16, value-size/u16, name, value records. Flag 1 means known length; otherwise length is UINT64_MAX. Names are compared case-insensitively; duplicates remain separate and values are bounded by the total response-head budget. Both HEAD and no-body final responses have known length zero.

Core service 0.2 WAIT_RESOURCE (4) accepts handle/u64 and interests/u32, and reliably completes with status/i32 followed by ready/u32. Masks are READ=1, WRITE=2, HEADERS=4, END=8. Completion does not consume data; callers retry and rearm after advisory readiness. Cancellation removes the wait, while exchange ownership determines transport cancellation. Each direction allows one concurrent producer/consumer.

Buffers are 512..65536 power-of-two bytes; upload and download split that total when uploading. Heads are 16..4096 bytes, subject to the product cap. Known upload length is limited to INT32_MAX by the ESP client API. Download budget is an independent u64; a response is never accumulated in memory just because its total limit is large. Redirects return to the Guest for another permission check; content-encoding is not automatically decoded. See [C++ API and implementation](../../docs/zh-CN/http-streaming.md).

## Outbound Socket streams (0.6)

`SOCKET_OPEN` (5) opens an owned, connecting TCP/TLS byte stream. Its ordered
required TLVs are endpoint (tag 1), permission/u64 (3), connection timeout/u32
milliseconds (8, 100–60000), and combined RX/TX buffer bytes/u32 (14, a power
of two from 512 to 65536). Core wire profile 1 is required.

Endpoints are `tcp://host:port` or `tls://host:port`, with an explicit nonzero
port, lowercase ASCII DNS/IPv4 or bracketed IPv6, and no userinfo, path, query,
fragment or leading zero port. The permission is `net.socket` scoped to the
exact endpoint, or `outbound` for an application with user-configured servers.
HTTP's `net.client/web` does not authorize raw sockets. Capabilities bit 16
indicates backend support; unavailable backends return `UNSUPPORTED`.

The result body is a u64 stream handle. Opening returns before DNS/connect/TLS
finish. READ (1) and WRITE (2) return a partial byte count or `WOULD_BLOCK`;
zero READ is clean EOF after draining buffered bytes. Errors are reported after
received bytes have been drained. POLL (3) uses READ/WRITE/END readiness flags.
STATE (4, empty input) returns `WOULD_BLOCK` while connecting, 1 when connected,
0 after clean closure, or the terminal negative status. TLS verifies both
certificate trust and peer name; there is no insecure fallback or redirect.

Each direction owns half the requested memory. Full RX stops network reads;
full TX returns `WOULD_BLOCK`. Closing, component termination or permission
revocation cancels the socket and releases its authority immediately. A worker
may retain bounded storage until an in-progress connection operation returns;
these workers still count toward the backend limit. WRITE acceptance means
queued locally, not delivered to the peer. The initial interface is outbound
TCP/TLS only; UDP, bind/listen/accept and half-close are reserved.

The C++ SDK exposes `Context::sockets().open` (also `Context::network().connect`), `Socket::read/write`,
`read_some/write_some`, `state` and `close`. Its `WebSocketClient` implements
RFC 6455 in Guest code on the same sockets, including handshake verification,
masking, fragmented messages, control frames, bounded messages and partial I/O.
WebSocket applications declare WASI `random` for handshake nonces and masks.

`CONNECTIVITY` (6, empty request) reports local interface/IP state without a
permission prompt: its u32 body is 0 unknown, 1 offline, or 2 online. This does
not probe the Internet or imply that a particular server is reachable. The
optional backend callback performs no remote I/O and exposes no MAC/IP details.
C++ callers use `co_await context.net().connectivity()`.

## Local network resources (0.7)

The additive `LOCAL_OPEN` (7) and `LOCAL_CAPABILITIES` (8) require Core wire
profile 1. Opcodes 1–6 and the 32-byte CAPABILITIES (4) response remain unchanged.
New capability bits are LISTEN=32, DATAGRAM=64, DISCOVERY=128, LOCAL_IPV4=256.
An absent provider reports zero local features; opening returns `UNSUPPORTED`.

### Endpoint and open request

All scalar fields are little endian. A 24-byte endpoint has family/u16 at 0
(4=IPv4, 6=IPv6), port/u16 at 2, interface scope/u32 at 4, and 16 raw network
address bytes at 8. IPv4 uses the first four bytes; scope and the remaining
12 bytes must be zero. Family support is independently reported by the Host.

LOCAL_OPEN has this fixed 48-byte prefix, followed by its three byte strings:

| Offset | Field |
| --- | --- |
| 0 | permission handle/u64 |
| 8 | kind/u16: listener=1, datagram=2, advertisement=3, browser=4 |
| 10 | flags/u16: listener NO_DELAY=1; datagram BROADCAST=2; others zero |
| 12 | bind/advertised endpoint, 24 bytes |
| 36 | combined buffer bytes/u32, power of two in 512..16384 |
| 40 | max items/u16 in 1..8: listener children or visible browser services |
| 42 | service type byte length/u16 |
| 44 | instance byte length/u16 |
| 46 | DNS TXT byte length/u16 |
| 48 | service type, instance, TXT (without terminators) |

Listeners require a `net.listen` grant; datagrams require `net.datagram`.
Scope `local-network` restricts peers to the selected subnet and loopback;
scope `network` permits any reachable IPv4 peer. The Core derives this access
from the live grant, never a Guest-controlled flag. Browsing and advertisements require
`net.discovery/local-network`. These are independent authorities. HTTP/web or
outbound socket grants do not grant permission to receive LAN connections.
Wrong-component, stale, revoked or wrong-name/scope handles are rejected before
provider allocation. Closing a permission revokes its dependent handles, including
accepted streams; closing only a listener preserves already accepted streams.
Component shutdown closes all its resources. A canceled/undeliverable open result
also closes the new handle.

Listeners/datagrams have empty strings; port zero asks for an ephemeral port.
Zero bind address selects the Host's active interface. Discovery uses a service
type `_name._tcp` or `_name._udp`: lowercase letters, digits and interior hyphens,
1..15 name bytes, total at most 21 bytes. Host appends `.local.`. Advertisements
require a nonzero port and 1..54 UTF-8 instance bytes, without controls, dots or
backslashes. Native provider appends a unique nine-byte suffix to fit a 63-byte
DNS label. TXT is at most 256 bytes of length-prefixed DNS-SD strings; key bytes
are printable ASCII, nonempty and separated from an optional binary value by
`=`. Empty metadata may be omitted or encoded as a single zero byte. Browser
requests have no instance, TXT, flags or port. Native browser caches are shared
and bounded to eight services; max-items further limits each browser's visible
set. Discovery describes endpoints; it does not define application messages.

Successful open completion contains the owned stream handle/u64. Creation is
asynchronous: STATE (IO 4) returns `WOULD_BLOCK`, then 1, or a terminal status.
Callers can wait for WRITE readiness during initialization. LOCAL_ENDPOINT
(IO 5) returns the bound 24-byte endpoint once ready. An interface/address change
terminates current native resources with `UNAVAILABLE`; callers reopen them.

### IO and backpressure

Listener ACCEPT (IO 7) returns exactly eight bytes containing a new Socket
handle, or `WOULD_BLOCK`. READ readiness indicates a queued connection. Accepted
streams use existing Socket READ/WRITE/POLL/STATE semantics, support endpoint
queries LOCAL (5) and PEER (6), and inherit the listener's authority. The service
reserves a stream slot before consuming a pending connection, and enforces both
component and global limits. Accepted streams' buffer budgets are per child.

Datagram WRITE (2) accepts endpoint:24 followed by one payload of 0..1200 bytes.
It admits the entire message or returns `WOULD_BLOCK`; it never admits a prefix.
READ (1) returns one source endpoint and payload with the same framing. A small
output buffer returns `LIMIT_EXCEEDED` and leaves the packet queued. The combined
budget is split equally into RX and TX rings; each queued packet consumes 26
bytes in addition to payload, so small rings impose a smaller effective packet
limit. Oversized incoming datagrams are dropped whole. Without BROADCAST flag,
broadcast destinations are denied. The provider enforces local-subnet/loopback bounds for `local-network`, allows
any reachable IPv4 peer for `network`, and does not offer multicast memberships.

Browser READ returns exactly one event with a 32-byte prefix: change/u8 at 0
(1 added, 2 updated, 3 removed), reserved/u8 zero at 1, instance/type/TXT lengths
as u16 at 2/4/6, endpoint:24 at 8, followed by instance, type (without `.local.`),
and raw TXT. Events are deduplicated. Full queues retain pending revisions for
retry; cache entries cannot be recycled before visible removals are queued.
TTL expiry and goodbye records produce removals. Output strings/views borrow
the caller's buffer. Treat discovery data as untrusted and validate the
application protocol/version before exchanging application state.

Native local resources additionally support STATS (IO 8), a 32-byte snapshot:
received payload bytes/u64 at 0, sent payload bytes/u64 at 8, dropped packet or
queue admission count/u32 at 16, RX queued bytes/u32 at 20, TX queued bytes/u32 at
24, configured combined buffer bytes/u32 at 28. WRITE success means queued;
closing can discard unsent bytes. No delivery or durability guarantee is added.

LOCAL_CAPABILITIES has empty input and a fixed 32-byte result body: features/u32
at 0; global/component resource ceilings/u16 at 4/6; minimum/maximum combined
buffer bytes/u32 at 8/12; maximum datagram payload/u32 at 16; maximum cached
services, instance bytes, TXT bytes and type bytes/u16 at 20/22/24/26; reserved
four zero bytes at 28. These are ceilings, not currently free slots. HTTP,
outbound sockets and local handles share the service's component/stream limits.

### Native discovery profile

Linux/macOS and ESP use the same bounded IPv4 backend, a shared Host worker and
one lazily created mDNS socket. Native network work and DNS parsing run outside
Guest imports. Work per iteration, cache size, packet sizes and resource count
are bounded. Unused workers stop, and discovery memory is freed when its last
resource closes. No worker or native stack is created per accepted connection.

Discovery uses [mDNS (RFC 6762)](https://www.rfc-editor.org/rfc/rfc6762) and
[DNS-SD (RFC 6763)](https://www.rfc-editor.org/rfc/rfc6763), with PTR, SRV, A and
raw TXT records, multicast IP TTL 255, cache expiry, goodbye, QU and legacy
unicast responses. This profile uses one selected interface and unique instance suffixes, with
three ANY probes at 250 ms intervals after 0..250 ms random jitter, including
all proposed A/SRV/TXT data in the Authority Section. Received Authority data
is compared as a sorted set using uncompressed canonical RDATA. Simultaneous
losers defer one second; live conflicting responses during probing cause rename and a five-
second retry delay. Established owners answer probes and restart probing when
conflicting responses arrive. Startup announcements occur twice one second
apart, with no unsolicited periodic announcements. Browsing backs off from
two seconds to one minute; complete cache entries do not send repeated address
queries. Shared hostname records remain live when one of several advertisements
closes. This is a bounded interoperable profile, not a full Bonjour replacement;
it does not yet implement full Known-Answer suppression, subtypes, service-type enumeration,
IPv6/multi-interface discovery or an OS discovery-daemon bridge. Windows and
web Hosts currently return no listener/datagram/discovery features. Existing
outbound socket support, when present, is reported independently as bit 16. Inbound TLS, half-close,
Internet matchmaking, NAT traversal and game-state replication are separate
future layers. See the [C++ integration guide](../../docs/zh-CN/local-network.md).
