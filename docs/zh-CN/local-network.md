# 通用 IP 通信、服务发现与网页消息连接（Net 0.8）

Net 0.7 增加通用 TCP 监听/接受连接、UDP 数据报以及 mDNS/DNS-SD 服务发布与发现。
传输可以使用内网或公网 IP；局域网发现是可选的独立能力。
这些接口属于通用网络服务，可以用于文件传输、设备控制、协作工具和游戏。
Host 不处理房间、玩家、存档或同步规则；应用自行定义消息协议。

## SDK 使用

C++ 使用 `<pxa/network.hpp>` 或 `<pxa/app.hpp>`，通过 `context.network()`
创建资源。`context.network().connect()` 复用原有 TCP/TLS 主动连接实现；
`context.sockets().open()` 与 `local_network()` 旧入口仍兼容。
打开前查询 `co_await context.network().capabilities()`，按
`connect()`、`listen()`、`datagrams()`、`discovery()` 和 `ipv4()` 检查 Host 支持情况。
不支持某项能力时，对应特性位为零，调用返回 `unsupported`。
回复中的缓冲/资源上限针对新增端点后端；主动连接保留 `SocketOptions` 的原有上限。

在 `package.json` 声明所需权限，按 name/scope 排序：

```json
"permissions": [
  {"name": "net.datagram", "scope": "local-network", "required": false},
  {"name": "net.discovery", "scope": "local-network", "required": false},
  {"name": "net.listen", "scope": "local-network", "required": false},
  {"name": "net.socket", "scope": "outbound", "required": false}
]
```

组件声明 `net` 服务；使用示例中的 Clock 让步时也声明 `clock`。
权限分别授权 UDP、发现、监听和主动连接，只声明实际需要的项。
主动连接可以申请完整的 `tcp://IP:port` 端点权限；`outbound` 适用于允许用户
选择服务器的客户端。HTTP 的 `net.client/web` 不提供以上权限。

### 直接输入 IP，不使用发现

```cpp
auto permission = co_await context.permissions().acquire("net.socket", "outbound");
if (!permission) co_return std::unexpected(permission.error());
auto socket = co_await context.network().connect(
    "tcp://192.168.1.20:31888", *permission, {.buffer_bytes = 4096});
if (!socket) co_return std::unexpected(socket.error());
```

此路径不申请发现权限、不发送 mDNS 查询。也可以使用可达的公网 IP，或者
`tls://hostname:port`。需要公网 UDP 时申请 `net.datagram/network`；需要接受
任意来源的 TCP 连接时申请 `net.listen/network`。声明中将对应 scope 改成
`network`，无需同时申请 `local-network`。Host 根据实际授权决定是否限制子网，
Guest 不能通过请求 flags 绕过权限。发现的权限仍只使用 `local-network`。

API 不自动创建路由器端口映射或提供中继。跨公网的接收端需要公网可达地址、
IPv4 端口映射、VPN 或其他网络接入方式；防火墙也需要允许相应端口。
仅输入对方内网 IP 不能穿越 NAT。

### 监听与收发

以下片段位于返回 `pxa::Task<void>` 的协程内，生产代码需逐项处理错误：

```cpp
auto permission = co_await context.permissions().acquire("net.listen", "local-network");
if (!permission) co_return std::unexpected(permission.error());
auto listener = co_await context.network().listen(*permission);
if (!listener) co_return std::unexpected(listener.error());
auto local = listener->local_endpoint(); // 默认动态端口
if (!local) co_return std::unexpected(local.error());
auto peer = co_await listener->accept();
if (!peer) co_return std::unexpected(peer.error());
std::array<std::byte, 256> buffer;
auto n = co_await peer->read_some(buffer);
if (!n) co_return std::unexpected(n.error());
```

接受的连接复用 `pxa::Socket`。TCP 是字节流，一次读取可能只有半条消息，
一次写入也可能仅接受部分数据；应用应使用长度前缀等方式分帧，并循环处理。
`read_some` 返回零表示正常 EOF。关闭 listener 不会关闭已接受连接；撤销
`net.listen` 权限会关闭 listener 和它派生的连接。RAII、组件停止和任务取消
沿用 Core 的句柄清理流程。初次创建会等待资源准备好，Guest 不执行阻塞 socket 调用。

### 发布与发现

```cpp
auto grant = co_await context.permissions().acquire("net.discovery", "local-network");
if (!grant) co_return std::unexpected(grant.error());
const std::array txt{std::byte{3}, std::byte{'v'}, std::byte{'='}, std::byte{'1'}};
auto advertisement = co_await context.network().advertise(
    "_pxademo._tcp", "PXA echo", *local, txt, *grant);
if (!advertisement) co_return std::unexpected(advertisement.error());
```

另一端 `browse("_pxademo._tcp", permission)`，随后
`co_await browser.next(buffer)`。事件包含 added/updated/removed、端点、实例名、
服务类型和原始 TXT 字节。名称和 TXT 视图借用传入缓冲区，覆盖缓冲区前复制
需要保留的数据。发现到服务不等于建立连接，也不保证对方可信；应用检查版本
和消息格式后再继续。服务类型遵循 `_name._tcp` / `_name._udp`，name 最多 15
个小写字母、数字或中间连字符。实例名最多 54 个 UTF-8 字节；Host 添加唯一后缀。
TXT 最多 256 字节，使用标准 DNS 长度前缀字符串，可以有二进制值，不是 JSON。

该发现使用标准 [mDNS](https://www.rfc-editor.org/rfc/rfc6762) 与
[DNS-SD](https://www.rfc-editor.org/rfc/rfc6763) 报文，不自定义发现广播格式。
目前实现 PTR/SRV/A/TXT、查询、TTL、下线通知和单播回复，以及本 profile 的
名称探测与冲突退让：随机等待 0..250 ms，发送三次间隔 250 ms 的 ANY 探测，
成功后相隔一秒发布两次。已占用的名称会更换后缀并等待五秒；同时探测按
规范比较完整、有序、去压缩的 RDATA，退让方一秒后重新探测。已声明的记录
收到冲突响应时重新探测，收到其他主机的探测时回复自身记录。
空闲发布者不周期广播；持续浏览的查询间隔从两秒逐渐增加到一分钟。
服务子类型、服务类型枚举、完整 Known-Answer 抑制、IPv6 和多网卡发现尚未实现，
不应将此视为完整 Bonjour 实现。

### UDP

`bind_datagram(permission)` 创建动态端口 UDP socket；用 `NetworkOptions.bind`
指定本地端口。`send_to(endpoint, payload, scratch)` 一次入队一个包，返回值是
payload 字节数；队列满返回 `would_block`，由应用稍后重试。`receive_from(buffer)`
返回发送端点和借用的 payload；缓冲区至少留出 24 字节端点头。空 payload 合法。
不足以容纳整包时返回 `limit_exceeded` 并保留队头，换较大缓冲区重试。
需要广播时显式设置 `NetworkOptions.broadcast = true`。

UDP 不保证送达、顺序或唯一性；可靠传输由应用协议或 TCP 提供。
`stats()` 可以读取已发送/接收 payload 字节、丢包或队列溢出计数和队列占用。
`send_to` 成功只表示已入队，立即关闭可能丢弃尚未发出的包。

## 内存、性能与限制

- 每个 listener/UDP/browser 默认 4096 字节 combined buffer，可选 512..16384
  的 2 次幂。收发各占一半，每个 UDP 包额外占 26 字节，1200 字节 payload
  需要至少 4096 字节 combined buffer。接受的每条 TCP 连接单独使用同样预算。
- 浏览事件也需要整条装入 RX 半区；要容纳全部允许的名称和 TXT，browser 的
  combined buffer 至少选择 1024 字节，默认 4096 字节。
- listener 的 `max_items` 限制其存活子连接数；browser 的 `max_items` 限制可见服务数，
  可选 1..8，默认 3。Host 共享发现缓存最多八项；全满时不无限增长。
- 原生后端全 Host 最多 16 个资源，产品默认每组件上限 12，与 HTTP/Socket 的
  stream 表共享额度。实际值应查询 capabilities；没有无限接收队列或隐式大包缓存。
- 所有本地连接共享一个后台工作任务，没有每连接线程/任务栈。缓冲区有界，
  TCP RX 满时停止读取，TX 满返回 `would_block`。Guest 通过 Core 资源等待唤醒。
  这些预算描述应用收发队列，原生 socket/lwIP、任务栈和发现缓存另计。
  旧 HTTP capabilities 的并发数仍按 HTTP worker 预算报告，不使用扩大的共享句柄额度。
  发现缓存/报文缓冲惰性分配；最后一个发现句柄关闭后释放，空闲 worker 退出。
- ESP 优先用 PSRAM 存放后端资源，缺少 PSRAM 时尝试普通 8 位堆；内存不足明确失败。
  后端最多 16 个资源和 16 KiB/资源是硬上限，不代表任意板子都具备这份内存。
- 当前原生后端用于 Linux/macOS 和 ESP 的一个活动 IPv4 接口。`local-network`
  限制子网及 loopback，`network` 允许任意可达 IPv4 对端；网络/地址变化令已有
  资源返回 `unavailable`，由应用重新创建。Windows 和网页 Host 尚未实现新增
  的监听/UDP/发现，现有主动连接以各 Host 的 capability 为准。没有服务端 TLS、任意组播入组、
  IPv6、跨子网发现、NAT 穿透或互联网中继。

## 可运行示例与 Host 接入

[`sdk/guest-cpp/examples/local-network`](../../sdk/guest-cpp/examples/local-network)
提供 server/client 两种构建：先发现服务，再完成 TCP 与 UDP 回声。
在父工作区根目录构建，输出保存在本地产物目录：

```sh
demo_dir="local/artifacts/local-network-demo/$(date +%Y%m%d-%H%M%S)"
PXA_LAN_DEMO_ROLE=server PXA_LAN_DEMO_ENDPOINT= tools/app.sh build local-network \
  --source-root deps/pxa-system/sdk/guest-cpp/examples --target simulator \
  --output "$demo_dir/server"
PXA_LAN_DEMO_ROLE=client PXA_LAN_DEMO_ENDPOINT= tools/app.sh build local-network \
  --source-root deps/pxa-system/sdk/guest-cpp/examples --target simulator \
  --output "$demo_dir/client"
```

示例的 TCP/UDP 都监听 31888，默认使用发现。需要 IP 直连时，客户端设置
`PXA_LAN_DEMO_ENDPOINT=tcp://对方IP:31888`；公网模式两端再设置
`PXA_LAN_DEMO_SCOPE=network`。直连模式完全跳过发现。

[`examples/ip-dns`](../../sdk/guest-cpp/examples/ip-dns) 是更小的固定公网 IP 示例：
不请求发现权限，向 `1.1.1.1:53` 分别发送普通 UDP 和 TCP DNS 查询，并验证
响应头、事务 ID 和来源。它需要网络允许访问该公共 DNS 服务，不提供中继。

两端使用不同模拟器 state root 并运行各自的包，成功后显示
`LAN_SERVER PASS` / `LAN_CLIENT PASS`。也可以在局域网内两台装有 Net 0.7
Host 的设备上运行；旧固件仅更新应用不会获得这些能力。设备和路由器应允许
局域网 UDP 5353 多播及应用端口；访客 Wi-Fi 或客户端隔离会影响发现/连接。

新 Host 将 `pxa_network_backend_t` 填入 `pxa_net_config_t.network`（原 `local` 字段同址兼容），旧配置结构
和旧 Net 操作保持兼容。可复用
`libpxa/adapters/network/pxa_lan_native.c`，提供活动接口和唤醒回调；Host 负责在
组件/运行时释放句柄后停止后端。协议及完整字段见
[Net 0.7 草案](../../spec/draft/net.md#local-network-resources-07)。C Guest 使用
`<pxa_network.h>` 构造固定头，后续通过 Core IO/WAIT_RESOURCE 操作句柄。

mDNS 编解码依赖固定版本 `mjansson/mdns`，采用 Unlicense，保留原始头文件、
LICENSE 和来源/哈希；没有链接 Avahi 等受限许可实现。

## 浏览器原生 WebSocket（Net 0.8）

浏览器没有原始 TCP、UDP、监听或 mDNS 权限。需要跨网页与设备连接的应用可在原生主持端实现标准 WebSocket 协议，在网页通过通用 `WebSocketClient::open_native` 或 C 的 `pxa_websocket_open`（`pxa_websocket.h`）连接。此接口也适用于聊天、设备控制和状态订阅；服务不解析应用消息。

先通过 `network_capabilities()` 检查 `native_websockets()` / feature 位 `512`，再申请 `net.socket/outbound` 或精确 `ws://`、`wss://` URL 的权限。操作 9 由 Host 原生后端实现，与现有 `WebSocketClient::open()` 在 Guest 内通过 TCP 握手的实现独立；不支持原生后端时返回 `UNSUPPORTED`。URL 不能含凭据或片段。原有 TCP、UDP 和服务发现操作及后端 ABI 前缀保持兼容。

连接返回的 Core 资源采用完整消息 IO：READ/WRITE 为一个 opcode 字节（1 文本、2 二进制）和整条消息；缓冲不足不会弹出消息，写入遇到背压返回 `WOULD_BLOCK` 且不消耗数据。文本需为有效 UTF-8。QUERY 使用操作 4，连接中返回 `WOULD_BLOCK`、就绪返回 1；失败返回错误。C++ `WebSocket` 包装器隐藏 opcode 前缀并沿用异步收发接口。资源计入每组件网络配额，权限撤销、关闭或组件退出会清理连接。

网页 Host 每组件最多八个网络流，最多四个待处理 HTTP 请求；单条 WebSocket 消息最大 16 KiB，接收队列与发送背压都受请求缓冲上限约束。生产网页需按浏览器策略处理混合内容、本地网络权限和 TLS 信任：HTTPS 页面使用受信任的 WSS 端点，设备明文 WebSocket 可通过 TLS 代理接入。应用不应把网页 WebSocket 的可用性解释成原始 TCP 能力。
