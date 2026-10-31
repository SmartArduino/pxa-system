# PXA 商城安装 ABI 0.5（ESP Host）

商城详情保留服务器颁发的短期下载票据。Guest 通过 `store-installer` 服务（ID 20、
版本 0.5）调用 opcode 2 下载，提交 `app_id_length:u8 | ticket_length:u16 |
container_size:u64 | container_sha256:bytes[32] | app_id | ticket`，收到标准
`status:i32 | staging_name:bytes[19]` 完成事件（失败时仅返回状态）。Host 私有
暂存名仅是本次请求的定位符，格式为 `.store-xxxxxxxx.pxa`；Guest 随后以
opcode 3 提交这 19 字节，由 Host 弹出安装确认，再返回 `status:i32`。
商城“安装”直接使用 opcode 1 完成下载、确认、安装；只有用户明确选择“下载”
才使用 opcode 2 保存离线包，之后通过 opcode 3 安装。不存在该服务时，Guest
直接显示安装失败。

安装服务不按商城发布者、App ID 或预置/普通安装位置限制调用方：任意运行中的
已安装、已启用签名 App 均可请求。清单应声明 `store-installer`，用于兼容性检查。
**每次安装、更新和离线包安装都需要 Host 原生弹窗确认；没有静默安装参数，
也不会记住上次的批准。** 只下载不会安装。用户拒绝、弹窗无法显示、请求取消或
请求方退出时，未开始的安装不执行；已获得批准并开始的原子事务安全完成。
Host 固定下载源由 `CONFIG_PXA_STORE_ORIGIN`
配置，要求 HTTPS，拒绝跨源重定向，并仅接受严格的 artifact 下载票据路径。
Guest 的 URL、摘要及大小都不授予安装权限；它们仅是下载条件。Host 在
独立任务中写入 Host 私有临时文件，限制为 4 MiB，核对 SHA-256 和字节数。
写入前检查容器大小与可用闪存，并保留 128 KiB 元数据/压缩整理余量；不足则
返回 QUOTA_EXCEEDED，不先写满分区。解压后的空间需求仍由签名事务安装器检查。
下载完成后调用已有容器签名/清单预检，核对预检后的实际 App ID。Host 弹窗展示预检
得到的应用名称、版本、发布者及权限数量，用户确认后再调用原有事务安装器。
Host 只接受当前调用方拥有的已校验下载文件名，不会把 Guest 提供的
任意路径交给安装器。下载完成后 Host 保存文件与索引，重启后恢复；未完成
文件在启动时清理。一步安装的临时容器在成功、失败或取消后删除。显式下载
得到的有效离线包仅在拒绝安装确认时保留；实际尝试安装后，无论成功或失败
均删除其容器和登记，避免长期占用空间；损坏的缓存也会删除。下载管理同步
刷新，失败可重新下载，旧版本和应用数据仍由安装事务保护。最多保留八个
已完成下载，用户也可手动删除以释放空间。

opcode 4 返回已安装列表：`count:u8 | (app_id_len:u8 | version_len:u8 |
identity_len:u8 | release_sequence:u64 | flags:u8 | app_id | version | identity)*`，
其中 `flags` 的 bit 0 表示可卸载（内置应用不可卸载）；
opcode 5 返回调用方拥有的下载：`count:u8 | (app_id_len:u8 |
staging_name:bytes[19] | app_id)*`；opcode 6 按已登记文件名删除；opcode 7
按 App ID 打开已安装应用。opcode 8 接收 1–64 字节 App ID，Host 只对唯一匹配、
非内置且非当前运行的已安装应用显示系统卸载确认弹窗；确认后调用事务卸载器，
取消和失败分别返回状态，应用商店随后刷新已安装列表。opcode 4 最多列出十二项，列表与文件名都由 Host
生成，不能使用任意路径。失败记录只保存在商城本次运行中，可从下载管理删除。

网络服务的单响应上限为 256 KiB；商城从 4 KiB 起按需扩容接收目录。
Guest 默认私有 FS 配额仍为 65536 字节。下载和暂存仍在 Host 完成，
不会赋予 Guest 读取 Host 暂存文件的权限。

下载时 Host 通过 opcode 0x8001 发送 `request_id=0` 的事件，负载为
`download_request_id:u64 | received_bytes:u64 | total_bytes:u64`（小端）。进度
事件按约 5% 节流，可能合并或丢失；opcode 1/2 的完成事件才是最终状态。
调用方停止时 Host 标记下载取消，网络事件停止写入并由 worker 删除临时文件；
已开始的原子安装事务继续完成，避免破坏已安装版本。重启清理未登记的
`.store-xxxxxxxx.pxa` 和未完成的索引临时文件，保留有效的显式离线下载。
当前版本尚无重试/断点续传和 Guest 主动取消事件；服务端过期票据由商城按下安装按钮时
刷新详情获取。桌面模拟器尚未实现该 Host 服务。
产品应确保商城源及可安装发布者信任列表均按部署环境配置；如果未来允许
非信任发布者，还需修改现有容器签名信任策略，不能绕过签名验证。

SDK 的 `pxa_store_request_package_status()` 保留同步返回的 `DENIED`、`BUSY`、
`RESOURCE_LIMIT`、`UNSUPPORTED` 等状态；返回 OK 仅表示接受请求，最终结果仍由
完成事件给出。原布尔返回辅助函数保留。商城不得把所有提交失败都解释成网络不可用。

## 容器安装兼容开关（PXA 0.7）

当前容器为 Container 0.2，支持分块认证和流式解压。旧 Container 0.1 安装由
`PXA_ENABLE_LEGACY_CONTAINER` 控制，C 宏默认 `0`，原生 Host 的 CMake 选项默认
`OFF`。ESP 产品通过 `CONFIG_PXA_ENABLE_LEGACY_CONTAINER=y` 开启，统一传给
Core、安装适配器和 PXADB。关闭时会裁掉旧容器的完整载荷校验与展开路径，
但保留已安装目录、旧私有存储及存档的正常读取，也保留新容器的 LZ4 支持。

标准 0.7 商城目录只提供 Container 0.2；开启兼容的自定义固件可手动安装旧容器。
旧版 Host 的目录和历史发行保持不变。关闭兼容不改变包签名、文件摘要或安装事务要求。
