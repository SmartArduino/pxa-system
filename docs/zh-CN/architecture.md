# 架构

[English](../architecture.md)

## 1. 范围

PXA System 负责应用发现与身份、生命周期、Intent、RPC、事件、角色、窗口、
surface、权限、主题和运行时集成。宿主负责线程、时钟、存储、密码学、网络、
音频、显示和输入驱动；核心只通过 SPI 使用宿主能力。

## 2. 分层

```text
应用
  -> Native binding / PXA wire binding
  -> 可移植 System Core
  -> Runtime provider 与 System provider
  -> 平台和 Renderer SPI
```

依赖只能向下。核心公共头不能包含运行时、renderer、ESP-IDF、FreeRTOS、LVGL
或 WAMR 类型。

## 3. 合约、绑定与实现

每项能力只有一份规范合约。Native binding 可跳过序列化，PXA binding 必须
跨沙箱编码并校验，但两者必须保留相同的身份、授权、取消、排序与错误语义。
IDL/JSON、生成头、codec、文档与 golden vectors 必须同步。

## 4. 所有权与线程

核心是显式创建的对象，不是全局单例。状态由单一 owner thread 修改且不可
重入；其他线程通过平台队列投递。大帧、音频和 framebuffer 使用 stream、
lease 或 handle，不进入普通事件队列。

## 5. Runtime provider

首批 provider 为 `native-static`、`wamr-wasm` 和 `wamr-aot`。运行时类型不属于
应用身份；同一应用更换 artifact 类型不能改变数据、权限、Intent 地址或角色。

## 6. UI 与合成

核心不暴露 widget 或 LVGL。应用提交逻辑 surface；compositor 管理层级、焦点、
安全区和输入路由；renderer 管理后端对象。可移植 UI 使用 PXA UI 0.3，游戏和
媒体可提交 raw surface。Native 应用也可协商后端专用 root，但必须声明 renderer
要求，且不再具备跨 renderer 可移植性。

## 7. 主题

主题快照包含有效明暗模式、对比度、语义颜色、字体、间距、圆角、动画、产品
主题 ID 和递增 generation。普通 UI 使用语义 token；Canvas 应用解析 palette
并在 generation 变化时重绘。未知 token 必须有安全 fallback。

## 8. 可替换系统角色

Home、Settings、状态栏、导航栏、锁屏、权限提示、安装器和文件选择器都是角色，
不是固定链接的单例。候选可以是 Native 或 PXA 应用；产品策略负责授权和优先级，
最小恢复 shell 必须始终可用。

## 9. 兼容边界

接口分为 App ABI、Provider SPI 和 Internal API。ABI/SPI 结构以 `struct_size`
开头，协议协商版本范围和 feature bits。产品配置、RTOS handle、指针和 renderer
对象均不得进入应用 ABI。
