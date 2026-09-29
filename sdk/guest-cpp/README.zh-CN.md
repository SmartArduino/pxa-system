# PXA C++23 Guest SDK（开发中）

此 SDK 是独立的 C++23 Guest 实现。它直接导入 `pxa_submit` 和 `pxa_io`，
不包含或调用 Guest C SDK。构建目标为 `wasm32-wasip1`，需要 WASI SDK 的
libc++。推荐使用仓库锁定的 WASI SDK 34；异常和 RTTI 默认关闭。

当前已实现的最小应用见 `examples/counter`：

```cpp
#include <pxa/app.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Counter {
    State<int> count{0};

    auto view() {
        return Column(
            Text("Counter").font(Font::title),
            Text(count),
            Button("Add one").on_click([this] {
                count.update([](int value) { return value + 1; });
            })
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(Counter)
```

`PXA_APPLICATION` 生成三项 Core ABI 导出。应用可选实现 `on_start`、
`on_event`、`on_foreground`、`on_background`、`on_stop`。`on_stop` 中不能
调用 Host 导入；`Transport` 会拒绝该调用。UI 视图在启动时挂载；
`State<int>` 变化后，框架在当前事件结束前合并并提交一次 PATCH。
控件节点 ID 和 UI 事务由 SDK 管理。

当前实现包括 Core 消息编解码、资源句柄、应用入口、有界协程和请求表、
基础声明式 Row/Column/Text/Button、整数状态绑定，以及 GameRender 的
上下文创建、清屏、矩形和精灵批次 DrawList。计划中的完整服务接口、
动态 UI/导航/虚拟列表尚未完成；独立开发包已可构建和打包示例，
但目前不能作为完整发布版 SDK。

构建应用时，CMake 中使用 `pxa_add_app`，并提供
`PXA_CPP_SDK_DIR`、`PXA_ARTIFACT_DIR` 和 `PXA_CMAKE_MODULE_DIR`。
`Pxa::Cpp` 包含运行时静态库，头文件中的模板只负责类型化 UI 与任务。
使用仓库中的 `tools/package/build_guest_cpp_sdk.sh <输出目录>` 生成可搬移的
开发包，其中有 CMake、打包工具、锁定的工具链信息、示例及 `VERSION`。
开发包没有 C Guest SDK 依赖；用户可在源码仓库之外编译和打包：

```sh
export WASI_SDK_DIR=/path/to/wasi-sdk-34.0
export PXA_APP_SOURCE_ROOT=/path/to/my-apps
export PXA_PACKAGE_OUTPUT_ROOT=/path/to/output
export PXA_SIGNING_KEY=/path/to/publisher-private.pem
bash /path/to/pxa-cpp-sdk/tools/package/package_app.sh \
    my-app simulator /path/to/output/pxa-my-app
```

开发包自带构建时所用的 `wamrc`，因此当前预编译包仅适用于相同的 Linux
主机架构；其他主机可以通过 `WAMRC` 指定匹配的编译器。WASI SDK 34 可由
打包脚本按锁定摘要下载，也可显式设置 `WASI_SDK_DIR`。发布时先更新 PXA
仓库根目录的 `VERSION`，开发包直接复制该值，不维护独立 SDK 版本号。
WASI SDK 34 链接 libc++ 后，当前最小应用也会导入 `clock_time_get`；
清单需要声明 `monotonic-clock` 和 `wall-clock`，打包校验会检查实际导入。
