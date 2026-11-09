# Browser Host 0.10.1

本版配合 DevKit/固件 0.10.0 和 Agent Kit 0.6.1，Guest Core 仍为 2.1。
运行时已经提供的 Window 0.5、Net 0.8、FS 0.6 和 GameRender 1.6，现在也能通过
商城在线玩的兼容检查；Core 服务版本修正为 0.2，与 Core ABI 2.1 分开表示。

商城使用的兼容表从同一接口注册表生成服务版本，保留浏览器实际支持的功能子集。
原生 WebSocket 声明功能位 512 和 `net.socket` 权限。网页仍没有原始 TCP、UDP、
监听与 DNS-SD 后端；需要联机的应用应查询实际能力并使用合适的 WebSocket 服务。

部署方应同时允许页面与 Worker 的 `connect-src` 使用 `ws:` / `wss:`，避免策略
拦截运行时已经支持的连接。HTTPS、混合内容、来源与应用权限检查继续生效。
官网和商城使用相同固定版本及摘要；旧版本资源保留供已打开的页面使用。

维护者重新构建示例时，PXA 的 CMake 配置是宿主侧构建工具，不在 WASI sysroot
中。示例显式按此规则查找配置，避免目标 sysroot 阻止 SDK 配置发现。
