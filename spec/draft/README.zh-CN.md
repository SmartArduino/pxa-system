# PXA 当前草案

[English](README.md)

这是合并后的唯一当前草案。Host 和 Package 使用 UI service 0.3，不再存在 UI 0.1
转换层。每个 `pxa-*.json` 都携带自己的 `protocol_version`。

主要说明文档：

- `architecture.md`：所有权与执行规则；
- `core-wire.md`、`lifecycle.md`：Core envelope、handle、请求和生命周期；
- `window.md`、`ui.md`、`surface.md`：窗口、UI 0.3 transaction/Canvas 和 surface；
- `clock.md`、`fs.md`、`storage.md`、`ipc.md`：基础异步服务；
- `sensor.md`、`device.md`、`net.md`、`audio.md`：权限绑定的设备能力；
- `work.md`、`permission.md`：后台工作、授权和撤销；
- `package.md`、`container.md`、`installer.md`、`app-management.md`：签名包、
  `.pxa` 传输、可恢复安装与目录管理。

Service ID：1 Core、2 Window、3 UI、4 Clock、5 FS、6 Storage、7 IPC、
8 Sensor、9 Net、10 Audio、11 Permission、12 Secrets、13 Work、14 WASI、
15 Device、16 Surface。Canvas 是 UI node 的定长 display-list 子协议，不是独立
service；Surface 是 Host-owned bulk-pixel BufferQueue。

兼容性上，Core 只由包的 `min_sdk`（最低 ABI）和 `target_sdk`（行为策略）
约束；它不是 Component 的普通 service requirement。各 Service 则独立声明固定
major 内的 minor 范围和必需 feature bits。旧式 `services: ["net"]` 表示构建 SDK
的当前 minor 为下限、该 major 的开放上限；需要更精确约束时使用带
`min_version`、`max_version`、`features` 的 service 对象。规范定义见
`package.md` 的 “Source package compatibility declarations”。

草案数值仍可变化；达到 1.0 后已发布 ID 不得复用。LVGL、WAMR、LittleFS 与
FreeRTOS 是实现细节，不是协议概念。实现要被视为权威前，必须同时具备 golden
vectors 和 malformed-input 测试。
