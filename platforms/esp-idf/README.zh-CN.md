# ESP-IDF 集成

[English](README.md)

仓库根目录是后端无关的 ESP component 入口。需要额外 ESP 依赖的可选适配位于
`platforms/esp-idf/components/`，产品通过 `EXTRA_COMPONENT_DIRS` 显式选择。
启用 PXA 执行时将 `wamr/` 作为 component 加入，其 component 名为 `wamr`。

板级服务、package 存储、产品 UI 与授权策略留在产品组件中。产品通过正常
provider/role 注册替换参考实现，不覆盖弱符号，也不让核心依赖 `app_pages`。

## WAMR

PXA 直接使用自身 `wamr/` 子模块中由 `config/wamr.json` 固定的提交。ESP-IDF
6.1 所需的声明兼容头位于
`platforms/esp-idf/wamr/wamr_espidf_compat.h`，不改变 WAMR 源码或 AOT ABI。

只校验提交固定关系而不进行 ESP 构建：

```sh
python3 tools/wamr/test_source_pin.py
```
