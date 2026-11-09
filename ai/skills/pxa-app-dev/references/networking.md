# PXA network transport choices

Use this reference for multiplayer, shared state, chat or device control. First
identify the peers, which peer hosts the session, and whether connections stay on
the same LAN or cross the internet. Discover the locked SDK's real declarations
with `context apps/name --query WebSocketClient` or `--query Network`.

## Choose from actual capabilities

DevKit/firmware/Browser Host 0.10 implement Net 0.8. Query
`co_await ctx.network().capabilities()` before selecting a transport. The release
number alone does not promise every backend on every operating system.

| Host | TCP client | TCP listener, UDP, DNS-SD | WebSocket client |
| --- | --- | --- | --- |
| ESP firmware | Yes, while connected | IPv4 provider; query each feature | `ctx.websockets().open()` over TCP/TLS |
| Linux/macOS desktop | Yes | IPv4 provider; query each feature | `ctx.websockets().open()` over TCP/TLS |
| Windows desktop | Yes | Currently unsupported | `ctx.websockets().open()` over TCP/TLS |
| Browser | Unsupported | Unsupported | `ctx.websockets().open_native()` |

`connect()`, `listen()`, `datagrams()`, `discovery()` and `native_websockets()`
test different feature bits. A WebSocket grant does not add raw sockets to a
browser. Browser networking uses its native WebSocket implementation, not an
HTTP-to-TCP tunnel through the PXA store. Browser and desktop capabilities can
therefore differ even while using the same Guest protocol and application code.

For a cross-browser game, use an application server implementing WebSocket/WSS.
An ESP/Linux peer may host a protocol implemented by the application on top of
its listener; the Host does not supply a room system or a WebSocket server API.
DNS-SD discovery is optional and local. Direct IP does not require discovery;
an internet receiver still needs a reachable endpoint, VPN, routing or a relay
chosen by the application. Do not promise NAT traversal from an IP address.

## One client path for native and browser Hosts

Declare the `net` service at minor 8 and the actual permission, for example
`{"name":"net.socket","scope":"outbound","required":false}`. Acquire it
through the SDK and handle rejection. A C++ coroutine can select the provider:

```cpp
auto capabilities = co_await ctx.network().capabilities();
if (!capabilities) co_return std::unexpected(capabilities.error());
auto grant = co_await ctx.permissions().acquire("net.socket", "outbound");
if (!grant) co_return std::unexpected(grant.error());
if (!capabilities->native_websockets() && !capabilities->connect())
    co_return std::unexpected(pxa::Error::unsupported);
auto connection = co_await (capabilities->native_websockets()
    ? ctx.websockets().open_native(url, *grant)
    : ctx.websockets().open(url, *grant));
if (!connection) co_return std::unexpected(connection.error());
```

The snippet belongs inside a `pxa::Task<void>` with `ctx` and `url` alive while
it runs. Keep both the grant and connection alive for the entire session; do
not return a connection whose local permission object is about to be destroyed.
For a persistent session, keep their ownership with the app's task or state.
Native browser WebSockets do not accept arbitrary HTTP headers. Put app-level
authentication in an appropriate message exchange and avoid logging credentials.
An exact `ws://`/`wss://` URL grant applies to `open_native`; the TCP-based client
uses a TCP/TLS endpoint grant or `outbound`, as shown above.

`WebSocket::write/read` use one opcode byte (1 UTF-8 text, 2 binary) followed by
one complete message. A short read returns `limit_exceeded` without consuming
the queued message. `would_block` means yield and wait for readiness; do not
busy-spin or treat an admitted write as proof the other peer received it. Net
0.8 does not add WebSocket `read_some`/`write_some` coroutine methods. Check the
locked SDK for its supported Core resource-wait/Clock interfaces before adding
an async wrapper. Keep messages, queues and reconnection attempts bounded.

On an HTTPS page use trusted WSS. A plain LAN `ws://` device endpoint may need a
TLS proxy and browser local-network permission; neither bypass mixed-content
checks nor silently relay traffic through the store. Treat blocked networking,
permission refusal and an offline peer as normal states with useful UI.

## Prove the application exchange

Run at least two actual endpoints and assert content, protocol version and
acknowledgements. Test native/native and browser/native when those targets are
requested, including disconnect, bounded reconnect, cancellation and app exit.
Use separate simulator state roots and ports. Device Wi-Fi must actually be
connected; a network feature bit is not a connectivity test. Router client
isolation can block LAN peers. Keep original apps, saved data and permissions.

For real UI tests, inspect a screenshot before resolving app nodes. A reference
device may be locked while its background Guest tree remains inspectable. Wake
the display and use the visible unlock gesture, then confirm the app is in front;
an input acknowledgement alone does not prove the intended button received it.
