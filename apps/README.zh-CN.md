# 参考应用

[English](README.md)

`apps/pxa/` 保存随仓库维护的参考 PXA 应用，用于覆盖 Guest SDK、包元数据、
i18n、语义主题、系统服务、Canvas 和多 Component 运行时。

普通页面、表单、状态文本和控件必须使用 PXA UI 的语义主题 token。游戏、
可视化与媒体可以使用 Canvas 或 raw surface，并维护自己的内容调色板；其
外围系统栏仍由 PXA System 主题控制。`weather`、`lab`、`arcade` 与
`wasi-lab` 的普通控件均遵循这一规则。

打包工具默认从 `apps/pxa` 读取应用。产品可以只选择其中一部分，也可以用
`PXA_APP_SOURCE_ROOT` 指向外部应用树。可移植系统库本身不依赖任何具体应用。

`apps/pxa/.dev-signing` 只包含开发测试密钥，不能用于生产。产品必须提供受
保护的签名流程和信任策略。

运行 `python3 tools/apps/check_ui.py apps/pxa` 可检查每个参考应用是否具有完整
源语言/中文目录、locale 生命周期处理，并检查普通 UI 入口是否使用语义主题
token。
