# 参考应用

[English](README.md)

`apps/pxa/` 只保留 `hello` 最小 Guest 界面测试应用及公开的开发签名夹具。
原参考应用源码迁至独立的 `pxa-apps` 仓库（工作区 `local/pxa-apps`）。
这些应用用于覆盖 Guest SDK、包元数据、
i18n、语义主题、系统服务、Canvas 和多 Component 运行时。

普通页面、表单、状态文本和控件必须使用 PXA UI 的语义主题 token。游戏、
可视化与媒体可以使用 Canvas 或 raw surface，并维护自己的内容调色板；其
外围系统栏仍由 PXA System 主题控制。`weather`、`lab`、`arcade` 与
`wasi-lab` 的普通控件均遵循这一规则。

打包工具默认从 `apps/pxa` 读取测试应用。构建产品应用时将
`PXA_APP_SOURCE_ROOT` 指向外部 `pxa-apps` 目录；系统库不依赖任何具体应用。

direct 构建的应用如果要长期保留 GuestMapped Surface buffer，必须在
`package.json` 中声明有界且固定的线性内存：

```json
"build": {
  "system": "direct",
  "linear_memory": {"maximum_bytes": 2097152, "pinned": true}
}
```

`maximum_bytes` 必须是 64 KiB WebAssembly 页的整数倍。打包器会把该上限写入
每个 Component module 并校验产物，再把 `pinned: true` 写入该 Component 的
已签名清单项。支持此能力的 WAMR Host 只会在实例化这个 Component 时预留完整
地址范围，使 `memory.grow` 不会移动已注册的像素地址。如果 Host 报告不支持
GuestMapped，该声明不会把固定映射能力强加给 Host。

`apps/pxa/.dev-signing` 只包含开发测试密钥，不能用于生产。产品必须提供受
保护的签名流程和信任策略。

运行 `python3 tools/apps/check_ui.py apps/pxa` 可检查测试应用；将参数替换为
独立应用仓库的路径可检查产品应用的本地化与语义主题。
