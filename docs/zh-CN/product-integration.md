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

## 权限策略编译开关

默认启用权限策略。产品需要直接允许全部 PXA 应用权限时，可在 CMake 配置中设置
`-DPXA_ENABLE_PERMISSIONS=OFF`；ESP Host 对应 `CONFIG_PXA_ENABLE_PERMISSIONS=n`。
构建系统统一向 Core 和 Host 发布 `PXA_ENABLE_PERMISSIONS=0`，使用轻量兼容实现替代
权限策略、决策持久化与请求弹窗，应用设置页也不编译权限开关。

关闭后，合法的 CHECK/ACQUIRE 请求立即允许，包括未声明的权限和作用域；
设置拒绝、撤销决策不会产生限制，也不会保存新决策。既有决策文件保留，重新启用
策略时可继续使用。Guest 的 32/64 位接口保持兼容，取得的权限句柄仍由 Core 管理：
关闭句柄释放其关联资源，组件停止时回收全部资源。无效消息、失效或跨组件句柄、
资源限额仍按原有接口处理。

此开关只控制 PXA 权限审批。签名与发布序号校验、应用私有存储隔离、硬件能力限制，
以及浏览器自身的串口、摄像头和麦克风授权继续生效。自行组织源码的产品应同时
选择 `permission_allow_all.c`，排除 `permission_service.c` 和 `permission_policy.c`，
并保证所有链接组件使用同一个宏值。


## 产品操作规范

产品固件可以在 Host/PXADB 工作线程启动前注册自己的实体按键、触摸、互斥组合和系统
保留手势，继续使用厂商自己的界面和路由。商城与 AI 读取有界描述并匹配应用签名清单中的
完整操作方案；不要求统一为某一种设备分类，也不从调试注入能力猜测实体输入。
接入 API、兼容语义和旧固件降级规则见 [设备交互描述](device-interaction.md)。
