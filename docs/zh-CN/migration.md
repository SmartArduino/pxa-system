# 迁移计划

[English](../migration.md) | [当前状态](implementation-status.md)

迁移以可验证的退出条件推进，不以“接口已添加”视为完成。产品现有 `app_pages`
同时承担导航、LVGL shell、业务回调、设备状态和 PXA 集成；目标是让这些职责分别
落入应用、PXA System 合约、renderer 和产品 provider。

## 里程碑

1. 可移植基础：统一身份和 registry，无 ESP/LVGL 公共依赖。
2. 生命周期与 runtime：Native/PXA 产生相同可观察 lifecycle。
3. Intent 与消息：跨 runtime 启动、RPC 和事件走同一合约。
4. Compositor/renderer：同一 UI 在 headless 与 LVGL 运行，Core 不暴露 LVGL。
5. 参考 shell/主题：Home、Settings、状态栏可由授权 Native/PXA provider 替换。
6. 产品应用迁移：逐个迁移 identity、lifecycle、Intent 和 service；直接 LVGL UI
   可作为兼容模式，之后再独立迁移到可移植 UI transaction。

目标依赖方向：

```text
application -> contract binding -> portable core -> provider SPI
native runtime -> portable core
PXA runtime -> portable core + libpxa
LVGL renderer -> renderer SPI + LVGL
ESP platform -> provider SPI + ESP-IDF
```

## Package 身份持久化

存储 key 使用 `<publisher-root-hex>~<app-id>`，外部身份使用冒号形式。旧 App-ID-only
目录必须先复验 manifest 和 publisher，再通过可恢复三 slot 事务迁移 package、
owner、private data、permission 与 disabled policy。冲突或无 owner 数据进入隔离，
不能依赖目录枚举顺序。runtime 类型始终不进入存储 key。
