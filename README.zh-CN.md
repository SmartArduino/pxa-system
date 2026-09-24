# PXA System

[English](README.md)

PXA System 是面向嵌入式产品的可移植应用系统。它统一原生应用与 PXA
应用的身份、生命周期、Intent、RPC、事件、系统角色、服务和 UI 合约。
`libpxa` 保留原名，负责平台无关的 PXA Host Core；WAMR、LVGL 和 ESP-IDF
均通过可选适配层接入，不进入核心公共接口。

## 目录结构

```text
pxa-system/
  apps/pxa/                   最小 Guest 测试应用及开发签名夹具
  cmake/                      源文件清单和 CMake 包配置
  config/                     WAMR 等集中版本元数据
  libpxa/                     平台无关的 PXA Host C99 库
  platforms/esp-idf/          ESP-IDF 适配、WAMR 补丁与源码覆盖
  sdk/                        Guest C SDK、Native C SDK 和 CMake 工具链
  services/                   标准及厂商服务提供者边界
  simulator/headless/         CI 用无界面模拟器
  simulator/desktop/          独立 SDL2/LVGL 标准 UI 模拟器
  spec/draft/                 合并后的唯一协议草案与 JSON 规范
  system/                     核心、运行时和标准组合根
  tools/                      打包、i18n 与 WAMR 校验工具
  ui/                         参考系统 UI、渲染器和后端辅助层
  wamr/                       固定提交的 WAMR 子模块
```

产品硬件服务、板级驱动和产品专用页面不属于可移植核心，应通过 provider
接口在产品仓库中注册。当前产品仓库中的 `components/pxa` 和 `app_pages`
是外部集成层，不应反向渗入 `pxa-system`。

## 获取与构建

```sh
git clone --recurse-submodules <pxa-system-repository>
cd pxa-system
cmake -S . -B /tmp/pxa-system-build
cmake --build /tmp/pxa-system-build
ctest --test-dir /tmp/pxa-system-build --output-on-failure
```

默认主机构建包含 `libpxa`、WAMR 适配、原生与 PXA 运行时、标准系统、
无界面模拟器和 SDL2/LVGL 标准 UI 模拟器。需要 OpenSSL、LZ4 与 SDL2
开发包；桌面模拟器还需要 libpng。桌面模拟器默认获取固定版本 LVGL 9.5；离线构建可传入
`-DPXSYS_LVGL_SOURCE_DIR=/path/to/lvgl`。

WAMR 必须保持在 `config/wamr.json` 指定的提交。该文件同时维护 WAMR
版本、AOT engine ABI 和 Espressif LLVM 提交。ESP 兼容补丁只应用到构建
目录 overlay，完整内存映射替代实现位于
`platforms/esp-idf/wamr/overrides`，不会修改子模块。

ESP 运行时仅在已签名包清单声明固定内存的 Component 实例化期间预留线性内存；
普通 Component 仍使用 WAMR 默认的可增长内存行为。

## ESP-IDF 集成

可把仓库放在 `components/pxa_system`，通过 Git component 引入，或将其
父目录加入 `EXTRA_COMPONENT_DIRS`。根 `CMakeLists.txt` 会在 ESP-IDF 环境
注册平台无关基础组件。LVGL renderer、WAMR、产品服务和产品 UI 均由产品
显式选择。

详见[文档索引](docs/README.zh-CN.md)、[产品集成](docs/zh-CN/product-integration.md)、
[桌面模拟器](simulator/desktop/README.md)与[统一规范草案](spec/README.zh-CN.md)。

## 兼容性

当前协议仍为 `0.x` 草案。公共结构使用 `struct_size`，wire 协议具有显式
版本，但在首个外部产品端口和完整一致性套件完成前不承诺稳定兼容。
