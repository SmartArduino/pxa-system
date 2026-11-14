# PXA Browser Host 0.11.2

状态：发布准备中，公共入口继续使用已发布版本。

HTTP 流式交换按调用者声明的响应头总预算接收字段，保留旧缓冲请求的字段限制。
有效响应头超出预算时返回 `limit_exceeded`，可通过可选 IO 7 `LIMIT_REASON`
查询响应头或正文分类；未知和旧 Host 仍可返回未知或不支持。取消及真实网络错误
保留原有状态，不因 Fetch 的后续完成覆盖终态。

已有 Core 与 Net 服务版本保持不变。该版本使用独立的浏览器运行时制品和 URL，
不覆盖历史归档。接口说明见 [HTTP 流式交换](zh-CN/http-streaming.md)。

## English

Preparation only; public channels retain their published runtime.
Streaming HTTP uses the requested total response-header budget while buffered
requests retain their field limits. Header capacity failures report
`limit_exceeded` with the optional IO 7 diagnostic. Cancellation and network
failures retain their original terminal result. Existing service versions remain
compatible; this runtime uses a distinct artifact version and URL.
