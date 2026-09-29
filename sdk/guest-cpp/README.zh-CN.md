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

导航应用提供 `navigation()`，返回其持有的 `ui::Navigator<>`，不再提供单一
`view()`。在 `on_start` 中排入首页：

```cpp
Result<void> on_start(Context&) {
    return routes.push<Home>(std::ref(model));
}
ui::Navigator<>& navigation() { return routes; }
```

每个页面类型提供 `view()`；`push<Screen>(args...)`、`replace<Screen>(args...)`
与 `pop()` 排入切换，框架在当前事件结束时提交。参数使用普通小值或
`std::ref(model)`，导航历史不保存隐藏页面的视图；返回会重新构造页面。
需要保留的状态放在应用模型。构建或提交失败会保留原页面；切换成功才
销毁原页面及其任务。页面可实现 `on_mount(TaskScope&)` 启动页面任务，
只有挂载提交成功后才调用这个方法。页面持有的局部 State 无需可移动。

默认 `Navigator<8, 4096, 96>` 为 8 层历史、每页 4096 字节、每组路由参数
96 字节。页面总存储为两个固定槽，共 8 KiB；切换时候选页和旧页短暂共存，
成功后旧页销毁。静态页面应用不启用这块存储。超预算页面有编译诊断，
历史满返回 `resource_limit`，同一事件重复排入切换返回 `busy`。
完整示例见 `examples/navigation`。

运行中的 UI 提交失败会保留页面，并调用可选的
`void on_error(Context&, Error)`；没有钩子时输出错误日志。容量不足等
可恢复错误不会使 Host 停止应用，可以在后续事件重试；协议错误仍返回
失败。首次挂载失败仍属于启动失败，应用不会以空页面继续运行。

`examples/storage` 演示异步读取和持久化。`ctx.storage().get(key, buffer)`
把值复制到调用方缓冲区并返回实际字节数；`set(key, value)` 使用默认 512 字节
封包缓冲区。较大的值可调用 `set(key, value, packet)`；最大 key/value 组合需
2140 字节的复用缓冲区，SDK 直接在其中生成最终协议包，不额外复制大值。协议允许最大
2048 字节，但设备 Host 可以配置更低的单值上限，超过时由 Host 返回错误。

`examples/audio` 展示申请 `audio.playback`、scope 为 `media` 的权限，再通过
`ctx.audio().open(permission)` 创建会话。清单和申请的 scope 必须完全一致。
权限和音频会话都是不可复制的资源对象。
open 成功后还需等待 `session.graph(gain_db_q8)` 成功，之后才能播放。
会话支持 tone、S16LE PCM 写入、Assets 预加载音效、流式音乐、gain/EQ、
查询和 flush。`music(path)` 返回已接受的播放实例号；这不表示解码器就绪，
应在 `on_event` 中用 `decode_playback(event)` 匹配会话与实例，分别处理
ready、ended、stopped、replaced、error。PCM 可以短写，`would_block` 交给
后续事件重试，不在回调内忙等。权限撤销后 Host 会关闭绑定资源，后续操作
返回错误；`decode_permission_revoked` 的字符串和字节视图仅在当前事件有效。

异步函数参数中的 `string_view`、`span` 和服务资源属于借用。直到任务完成或
取消，调用方必须保持它们及服务资源有效；默认封包缓冲区来自有界协程池。

当前实现包括 Core 消息编解码、资源句柄、应用入口、有界协程和请求表、
基础声明式 Row/Column/Text/Button、整数状态绑定，以及 GameRender 的
上下文创建、清屏、矩形和精灵批次 DrawList。计划中的完整服务接口、
动态 UI/虚拟列表尚未完成；独立开发包已可构建和打包示例，
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
