# PXA Network Draft 0.1

Network service 0.2.0 provides bounded, Host-managed HTTP(S) requests. Opcode 1 is
the frozen v1.0 GET operation. Opcode 2 adds GET, HEAD, POST, PUT, PATCH and
DELETE, bounded request headers and inline bodies, per-request timeouts,
selected response headers, response length metadata and optional response
streams. A 0.2 Host must continue accepting the 0.1 wire format unchanged.

## Security and URL model

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
| 9 | request header | zero to 8 unique nested name/value records |
| 10 | inline request body | zero or one, at most 2048 bytes |
| 11 | wanted response header | zero to 8 unique header names |

GET and HEAD have no request body. Header names are lowercase HTTP token bytes,
1 to 64 bytes. Values are at most 256 printable ASCII bytes, with horizontal
tab also allowed. The total encoded request-header block is at most 2048 bytes.
Guest-controlled `connection`, `content-length`, `host`, `proxy-connection`,
`te`, `trailer`, `transfer-encoding` and `upgrade` are forbidden. The Host owns
framing, authority and hop-by-hop behavior.

Wanted response headers form an allowlist. The provider returns only available
headers named by that list, with no duplicates. This avoids exposing ambient
or sensitive headers the Guest did not request.

The success result records are also in ascending tag order:

| Tag | Field | Cardinality and rule |
| --- | --- | --- |
| 5 | status code | exactly one u16 in 100..599 |
| 6 | content type | exactly one printable ASCII value, at most 96 bytes |
| 7 | body Stream Handle | present exactly when BODY_PRESENT is set |
| 9 | selected response header | zero to 8 unique nested name/value records |
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

## Resource limits

The portable profile allows at most two pending requests per Component, eight
request or selected response headers, 2048 encoded header bytes, a 2048-byte
inline request body and a 262144-byte response. Hosts may configure smaller
limits and report `resource-limit`, `invalid-argument` or `limit-exceeded` as
appropriate. Core uses fixed Host-provided workspaces and bounded slot tables;
the ABI does not require allocation on the request path.

WebSocket, TCP, UDP, listening sockets, multipart construction, cookie jars,
automatic decompression, redirects and streaming request bodies are outside
Network 0.2.
