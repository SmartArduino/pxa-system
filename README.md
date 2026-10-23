# PXA System

[English](README.en.md)

PXA（Portable eXecutable Application，可移植可执行应用）是面向嵌入式产品的应用系统。应用以签名 `.pxa` 包独立开发、安装和更新；系统为原生应用与 PXA 应用提供统一的身份、生命周期、界面、权限和服务契约。核心不依赖 LVGL，产品可以选择自己的渲染器和驱动。

## 开发 C++26 应用

下载 [DevKit 0.5.0](https://github.com/SmartArduino/pxa-system/releases/tag/v0.5.0)。完整包包含 SDK、WASI/AOT 工具链、Python、CMake/Ninja、PXADB 与预编译模拟器，无需先构建固件。当前二进制要求 Linux x86_64、glibc 2.39+。

```sh
export PATH="$PWD/pxa-devkit-0.5.0-linux-x86_64/bin:$PATH"
pxa init hello
pxa run hello
# 关闭模拟器后再打包：
pxa build hello --target esp32s3,esp32s31,simulator --offline
pxa publish hello/dist/pxa-hello.pxa --check-only
```

默认 C++26、O3，CMake 是唯一的源文件与依赖描述；`pxa.lock` 固定发行版本和工具链输入。开发私钥在 SDK 外的用户数据目录生成。正式发布使用自己保存的长期密钥。

[入门教程](https://pxa.doit.am/docs/first-app/) · [C++ SDK](sdk/guest-cpp/README.zh-CN.md) · [架构与协议文档](docs/README.zh-CN.md)

[PXA Agent Kit](ai/README.md) 提供一句话环境准备、Codex/Claude Code/DSH 接入、MCP、真实 Guest 调试、USB 安装、官方固件升级与商城短码授权。支持 DevKit 0.5.0，独立签名发行，不增加设备运行开销。把这句话交给编程助手：

> 请读取 https://pxa.doit.am/ai.md，在当前目录为我配置 PXA 开发环境，运行模拟器检查环境，然后等待我的应用需求。

## 目录与边界

```text
libpxa/                    可移植 Host 运行库
system/core/               身份、生命周期、Intent、RPC、角色与主题
system/runtimes/           原生与 PXA 应用运行时
system/standard/           标准系统组合入口
ui/                        系统界面、渲染器与可选 LVGL 后端
services/                  系统与产品服务接口
sdk/guest-cpp/             持续维护的 C++26 Guest SDK
tools/pxa_tools/                   独立 DevKit 命令行与分发工具
sdk/templates/             pxa init 使用的应用模板
sdk/cmake/                 WASI 工具链与应用 CMake 支持
sdk/guest-c/                旧绑定与协议参考，新应用使用 C++ SDK
wamr/                      固定提交的运行时子模块
config/                    运行时与编译器版本约束
platforms/esp-idf/          ESP-IDF 集成支持
simulator/                 无界面及 SDL2/LVGL 桌面模拟器
spec/                      协议、服务规格与测试向量
tools/package/             清单、签名与打包工具
scripts/                   Host 构建、测试与 SDK 发布入口
```

板级驱动与产品业务不放入可移植核心。产品通过服务、渲染器和角色接口接入自己的实现。原生应用链接原生绑定，PXA 应用使用 Guest SDK。

## 从源码构建系统

以下命令面向系统维护者，与上面的独立应用开发二选一：

```sh
git clone --recurse-submodules https://github.com/SmartArduino/pxa-system.git
cd pxa-system
cmake -S . -B /tmp/pxa-system-build
cmake --build /tmp/pxa-system-build
ctest --test-dir /tmp/pxa-system-build --output-on-failure
```

Host 构建需要 C/C++ 编译器、CMake、OpenSSL、LZ4、SDL2 与 libpng 开发包。桌面模拟器默认获取固定版本 LVGL 9.5；离线构建可设置 `PXSYS_LVGL_SOURCE_DIR` 指向已有的 LVGL 9.5 源码。详见[桌面模拟器](simulator/desktop/README.md)。

`scripts/test-host.sh` 提供相同的默认构建与测试流程。`scripts/package-sdk.sh` 生成可安装的原生系统 SDK；它与面向 Guest 应用的 DevKit 是不同交付物。

## 运行时与工具链

`config/wamr.json` 固定 WAMR 提交（当前标识 `78-wamr-6a54186a`）、AOT 引擎 ABI 和 Espressif LLVM 提交。更新运行时需核对 ESP 补丁、更新元数据并重新生成 AOT，不应只替换某个编译器二进制。

`tools/package/build_wamrc.sh` 使用本仓库的 `wamr/`；生成源码与编译缓存在 `.pxa/`，可用 `PXA_WAMR_CACHE_DIR` 重定位。缓存键包含 WAMR 与 LLVM 提交。工具保留 X86、Xtensa、ARM、AArch64、Mips 与 RISC-V 后端；新增目标仍须补齐打包和设备支持。

Guest 编译使用 WASI SDK 34.0（Clang 23）。源码打包工具依次读取 `PXA_WASI_SDK_DIR`、`WASI_SDK_DIR`、`WASI_SDK_PATH`，再检查 `/opt/wasi-sdk` 和缓存。`PXA_WASI_SDK_AUTO_DOWNLOAD=0` 禁止自动下载，`PXA_WASI_SDK_CACHE_DIR` 改变缓存目录。DevKit 自带工具链，不需要另行下载。

ESP 运行时只为清单明确声明 pinned memory 的组件保留固定线性内存；普通组件保持可增长内存行为。

普通 AOT 支持从 Flash／SD 的文件后端分段读入最终代码区，降低启动峰值；也可显式选择 Wasm 解释执行。接入、内存边界与验证方式见 [AOT 加载说明](docs/zh-CN/aot-loading.md)。当前不支持 XIP。

## ESP-IDF 集成

将本目录作为 ESP-IDF component，或将父目录加入 `EXTRA_COMPONENT_DIRS`。根 `CMakeLists.txt` 自动识别 IDF；核心组件不要求 LVGL。

需要 LVGL 渲染器时，将 `platforms/esp-idf/components` 加入组件搜索路径，并依赖 `pxa_system_renderer_lvgl`。执行 PXA 时还需引入 `wamr/`。ESP 兼容配置集中在 `platforms/esp-idf`，产品代码调用该入口。

[产品集成](docs/zh-CN/product-integration.md) · [显示与系统 UI](docs/zh-CN/display-and-system-ui.md) · [ESP 产品工作区](https://github.com/SmartArduino/pxa-workspace)

## 兼容性

当前维护的 Guest ABI 是 Core 2.0，导入接口为 `pxa.core.v2::pxa_call` 和 `pxa_io`。C++ 源码 API 仍处于 1.0 之前，允许演进；使用 `pxa.lock` 固定输入，显式升级后重新编译和测试。Host 结构携带 `struct_size`，线协议使用明确版本。

AOT 引擎 ABI 与 Guest Core 版本分别管理：引擎标签中的 `core-1` 不是 Guest Core 1。旧 Core 1 包不能直接在 Core 2 Host 运行；设备还须满足应用声明的服务、功能与目标架构要求。
