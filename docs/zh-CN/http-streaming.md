# 有界流式 HTTP 与异步资源等待

网络服务 0.4 在已有 `pxa_call` 和 `pxa_io` 上增加流式交换。正文仍通过资源句柄读写，控制请求仍走服务路由。没有为每种协议新增 Wasm 导入，也没有把下载正文塞进控制消息。旧 `NetService` / opcode 1、2 保留，现有应用可以按需迁移。

## C++ 使用方式

```cpp
auto authority = co_await context.permissions().acquire("net.client", "web");
if (!authority) co_return std::unexpected(authority.error());

auto reply = co_await context.http().get(url, *authority, {
    .buffer_bytes = 4096,
    .header_bytes = 1024,
    .max_body_bytes = 8 * 1024 * 1024,
    .timeout_ms = 30'000,
});
if (!reply) co_return std::unexpected(reply.error());
// HTTP 404、500 仍是有效 HTTP 响应，需要应用判断 status_code。
for (auto cookie : reply->headers.values("Set-Cookie")) {
    // 保留重复字段；比较字段名时忽略大小写。
}
for (;;) {
    auto amount = co_await reply->body.read_some(buffer);
    if (!amount) co_return std::unexpected(amount.error());
    if (*amount == 0) break; // 成功 EOF；短读不是 EOF。
    consume(std::span{buffer}.first(*amount));
}
```

`buffer` 需要跨等待保持有效；可以放在应用对象中。`get()` 和 `open()` 在返回惰性 Task 之前复制请求描述文本，因此临时 URL、请求头和小正文不会悬垂。请求编码仅分配实际大小；每次正文读取/写入不分配堆对象，不额外占用 Task 池槽。

`HttpResponse` 拥有实际编码大小的响应头和交换句柄。字段字符串视图借用该响应头，响应对象移动时地址保持有效；销毁响应后不能再使用这些视图。`values()` 自己保存查询字段名，允许查询参数是临时字符串。默认空对象不分配内存。

只需要少数字段时，通过 `HttpRequest::wanted_headers` 在 Host 写入响应头缓冲前筛选：

```cpp
constexpr std::array<std::string_view, 3> fields{
    "content-type", "content-encoding", "location"};
auto exchange = co_await context.http().open(
    {.url = url, .wanted_headers = fields}, *authority);
if (!exchange) co_return std::unexpected(exchange.error());
auto filtered_reply = co_await std::move(*exchange).response();
```

留空表示保留全部字段；最多选择 8 个，名称不区分大小写，重复、空名称和非法名称会被拒绝。
筛选名称同样在返回 Task 前复制，数组和名称字符串不必跨等待存活。
正文长度和传输编码的协议校验在筛选之前进行，`body_length` 仍然有效。
例如文字阅读器不使用 CSP 或 Cookie 时，可保持原有缓冲预算，避免大型无用字段导致
`limit_exceeded`。需要处理重定向、内容压缩或 Cookie 的应用应显式保留相应字段。
这是对已有 tag 11 的 SDK 封装，无新增 ABI 或 Host 缓存。

上传使用 `open({.url=url, .method=HttpMethod::post, .upload_bytes=total}, permission)`，循环 `write_some()` 处理短写，最后 `finish_upload()`，再 `co_await std::move(exchange).response()`。上传总长已知；尚未支持未知长度的流式上传。服务器可以提前拒绝上传；Host 仍会接收最终响应，写入会停止。下载和上传共用调用者指定的总缓冲预算，各占一半。

句柄采用 RAII。取消 Task 会注销等待；销毁交换会取消传输。若取消一个等待但保留交换，传输可以继续，直到缓冲满触发背压。每个方向只允许一个操作同时访问缓冲；应用需要自行协调并发使用和借用存储寿命。

## ABI 与能力

- net opcode 3 `HTTP_OPEN`：现有 HTTP 请求记录，tag 4 改为 u64 总正文上限；新增 tag 14/u32 缓冲字节、15/u64 上传长度、16/u32 响应头字节。成功完成数据是交换句柄 u64。
- net opcode 4 `CAPABILITIES`：返回下载、上传、提前响应头能力及 Host 当前上限。SDK 的 `capabilities()` 用于特性探测；旧 Host 返回 unsupported。
- Core 服务 opcode 4 `WAIT_RESOURCE`：handle/u64 + interests/u32。一次性、可取消、可靠完成；完成载荷为 readiness/u32。注册时先检查资源，后续 Host 在网络通知时再次检查。关闭、错误和结束也唤醒等待；SDK 重试 IO，必要时重新等待，处理伪唤醒。
- `pxa_io`：1 读、2 写、3 查询 readiness、4 复制不可变响应头、5 查询头大小、6 完成上传。响应头由 16 字节固定前缀及紧凑的 name/value 记录构成，保留重复字段。

readiness 位为 read=1、write=2、headers=4、end=8。IO `would-block` 是暂时不可用；读取 0 是成功 EOF。已有数据先被读完，再返回终端错误。传输失败不会伪装成 EOF。

缓冲支持 512..65536 的 2 次幂；响应头支持 16..4096 字节，默认只请求 1024 字节；需要更大响应头时显式提高预算，不受旧控制完成工作区大小限制。响应头按总字节预算限制，不再把所有字段值硬性截到 256 字节。上传上限受 ESP HTTP API 的有符号 int 长度限制，为 2 GiB−1；下载总预算采用 u64，与驻留内存无关。控制包本身继续受 Core 的 4076 字节载荷上限约束。

重定向返回 3xx，由 Guest 决定下一次请求并重新检查权限。传输不自动解压 Content-Encoding；内容编码和 Cookie 策略属于更高层能力。TLS、DNS、证书验证和网络连接仍由 Host 管理。矛盾的 Content-Length / Transfer-Encoding 或互相矛盾的重复长度被拒绝。

## 内存与实现

公共层使用固定容量 SPSC 环形缓冲，以 32 位 acquire/release 发布读写位置。无正文 realloc、无关闭中断的大块复制。ESP 后端复用现有四个请求槽和工作线程，直接向环形缓冲的空闲段读取；缓冲满时让出该请求。ESP 在不增加 HTTP 库 512 字节缓冲和环形缓冲预算的情况下，每次最多处理 4 KiB 正文，摊薄运行线程唤醒开销；网络唤醒合并，避免每个数据块额外排入维护命令。桌面后端用 libcurl，接收背压通过条件变量，上传暂缺数据时暂停传输并继续处理提前响应。

Core 等待复用现有 lease 槽，完整请求标识防止取消后重用 token 时误唤醒。32 位目标通过复用字段保持每槽大小不变。网络服务去掉可推导的指针和容量字段，并缩小已核对上界的完成工作区，抵消新增回调的元数据。网络服务头到含 u64 的 pending 数组之间显式保证 8 字节对齐，防止 32 位目标上的结构变化引入未对齐访问。

这不是跨 Guest/Host 的零拷贝：权限、句柄代数和 Guest 指针边界仍然需要验证，读取时存在一次向 Guest 的复制，libcurl/ESP HTTP 库也有自己的有界传输缓冲。优化去掉的是全量正文驻留和每块数据的控制编码/协程分配，不承诺消除底层库复制。

连接、DNS/TLS 初始化仍可能暂时占用 ESP 的共享网络工作线程；已有旧缓冲请求也可能阻塞这个线程。分块背压改善的是正文阶段，连接公平调度和连接复用仍需后续独立实测。浏览器的 HTML、布局、解压、Cookie 和缓存不属于这一层。

验证示例见 [`http-stream`](../../sdk/guest-cpp/examples/http-stream/README.zh-CN.md)。
