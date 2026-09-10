# ESP-IDF 集成

[English](README.md)

仓库根目录是后端无关的 ESP component 入口。需要额外 ESP 依赖的可选适配位于
`platforms/esp-idf/components/`，产品通过 `EXTRA_COMPONENT_DIRS` 显式选择。
启用 PXA 执行时将 `wamr/` 作为 component 加入，其 component 名为 `wamr`。

板级服务、package 存储、产品 UI 与授权策略留在产品组件中。产品通过正常
provider/role 注册替换参考实现，不覆盖弱符号，也不让核心依赖 `app_pages`。

## WAMR 补丁

`wamr/patches/series` 是补丁顺序的唯一来源。配置时
`tools/wamr/prepare_overlay.py` 校验 `config/wamr.json` 中的固定提交和精确 hunk，
只把修改文件写入 build overlay。`wamr/overrides/espidf_memmap.c` 是明确由项目
维护的完整替代实现，CMake 将它替换进 WAMR target。整个过程不修改子模块。

补丁处理 ESP POSIX 类型、libc-WASI file ops、双总线 executable memory 清零和
Xtensa div/rem relocation。ESP32-S3 AOT 使用 windowed ABI 与明确的 `esp32s3`
CPU；LLVM 固定版本、提交和 engine ABI 全部由 `config/wamr.json` 管理。

只校验补丁而不进行 ESP 构建：

```sh
python3 tools/wamr/test_overlay.py
```
