# 产品集成

[English](../product-integration.md)

## 组合边界

`pxsys_standard_system_create()` 创建 registry、runtime、task manager、role host、
theme、renderer host、service registry 与 event broker，但不选择产品桌面、设置、
硬件驱动或存储策略。产品负责绑定 renderer、应用、角色候选、服务和策略，然后
启动常驻角色与 Home。

## 可替换系统 UI

Home、Settings、状态栏、导航栏、锁屏、权限提示、安装器、应用管理器和文件选择器
都通过角色注册。参考布局与 LVGL 实现在 `ui/reference/`。产品通过更高优先级且
已授权的候选覆盖，不替换符号、不 patch 核心，也不新增第二套导航协议。

```text
components/
  pxa_system/                 本仓库
  product_system_profile/     组合、容量与授权策略
  product_system_ui/          产品 Native 角色与 renderer 资源
  product_device_services/    板级传感器、存储、音频和网络
```

## 服务、主题与依赖方向

标准接口使用 `system.*`；产品扩展使用反向域名与语义版本。native handle 与 ESP
驱动类型留在 provider 内部。主题 provider 发布完整语义快照。

```text
产品 UI/服务 -> PXA System 合约
ESP 适配     -> PXA System SPI + ESP-IDF
PXA runtime  -> PXA System Core + libpxa
LVGL renderer-> Renderer SPI + LVGL
System Core  -> C99
```

ESP 集成和 WAMR patch/override 见
[`platforms/esp-idf`](../../platforms/esp-idf/README.zh-CN.md)。
