# ESP-IDF 集成

[English](README.md)

仓库根目录是后端无关的 ESP component 入口。需要额外 ESP 依赖的可选适配位于
`platforms/esp-idf/components/`，产品通过 `EXTRA_COMPONENT_DIRS` 显式选择。
启用 PXA 执行时将 `wamr/` 作为 component 加入，其 component 名为 `wamr`。

板级服务、package 存储、产品 UI 与授权策略留在产品组件中。产品通过正常
provider/role 注册替换参考实现，不覆盖弱符号，也不让核心依赖 `app_pages`。

## MicroPixel WAMR

PXA 使用根工程 `third_party/micropixel` 引用的 MicroPixel WAMR fork。PXA 的
`wamr/` gitlink 与该 fork 固定在同一提交；CMake 不再创建 build overlay、应用
补丁或替换 WAMR 源文件。ESP-IDF 6.1 所需的声明兼容头直接引用 MicroPixel 的
`wamr_espidf_compat.h`，不改变 WAMR 源码或 AOT ABI。

只校验提交固定关系而不进行 ESP 构建：

```sh
python3 tools/wamr/test_source_pin.py
```
