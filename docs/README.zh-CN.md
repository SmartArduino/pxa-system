# PXA System 文档

[English](README.md)

PXA System 定义 RTOS 或桌面宿主之上的应用环境，包括身份、生命周期、发现、
导航、通信、系统服务、UI surface、主题、权限、安装和可替换系统角色。它不
替代 FreeRTOS、ESP-IDF、Linux 或设备驱动，而是通过平台与服务 provider
接口使用这些能力。

## 设计目标

1. 原生应用和沙箱 PXA 应用共用身份、生命周期、服务命名与通信合约。
2. 原生应用使用 Native binding，不依赖 Guest SDK；PXA wire binding 进入同一实现。
3. 核心和应用合约不依赖 LVGL、ESP-IDF、FreeRTOS 或 WAMR。
4. LVGL 只是一个 renderer；系统体验通过角色选择，可被产品实现替换。
5. 厂商服务使用命名空间扩展，公共 ABI/SPI 均有版本与一致性向量。

## 文档地图

- [架构](zh-CN/architecture.md)
- [应用与协议模型](zh-CN/app-protocol.md)
- [PXA Binding](zh-CN/pxa-binding.md)
- [可移植性与扩展](zh-CN/portability.md)
- [产品集成](zh-CN/product-integration.md)
- [显示配置与标准系统 UI](zh-CN/display-and-system-ui.md)
- [本地化与字体](zh-CN/localization-and-typography.md)
- [迁移计划](zh-CN/migration.md)
- [实现状态](zh-CN/implementation-status.md)

协议数值、字段和版本以 `../spec/draft/*.json` 为唯一真源。中英文说明必须与
机器规范一致；发生歧义时以 JSON 与 golden vectors 为准。
