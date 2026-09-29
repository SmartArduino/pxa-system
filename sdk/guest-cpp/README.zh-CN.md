# PXA C++26 Guest SDK（开发中）

此 SDK 是独立的 C++26 Guest 实现。它直接导入 `pxa_submit` 和 `pxa_io`，
不包含或调用 Guest C SDK。构建目标为 `wasm32-wasip1`，需要 WASI SDK 的
libc++。推荐使用仓库锁定的 WASI SDK 34；异常和 RTTI 默认关闭。
语言模式与必需特性在配置和公共头文件中检查，不静默回退。
实际支持范围见 `FEATURES.zh-CN.md`；反射和 `template for` 不作为依赖。

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

内置视图在编译期推导绑定、回调和动态模块容量，布局汇总子视图预算；
没有动态模块的页面不会预留其槽。`Text(state)` 预留一个绑定，字符数组与
固定描述不预留绑定。动态 `std::string` 文字目前也保留一个绑定上限。
自定义视图可声明 `static constexpr ui::Capacity capacity`；
未声明时沿用 32 绑定、32 回调、4 动态模块的有界预算，也可显式配置 `Page`。
状态订阅使用脏绑定位图；最多 64 个绑定只需一个 64 位字。
属性更新直接枚举置位项，不扫描所有绑定。同周期对同一状态多次赋值只
编码最终值；提交失败保留位图以便重试。未变化的 UI 不提交事务。

`Text("Title")` 拥有字符数组的副本，不构造 `std::string`，也不会借用局部数组。
固定标题可写为 `Text<"Title">()`：文字由该类型的共享只读描述持有，页面仅保存
字体与颜色属性。静态布局支持 `constexpr`；状态引用和捕获闭包仍由实际页面持有。
动态字符串使用 `Text(std::string)`，状态文字使用 `Text(state)`，异步借用规则
不因固定描述而改变。

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

动态列表位于 `<pxa/list.hpp>`，模型持有 `ui::ListState`，行使用业务 key：

```cpp
ui::ListState items{10000};
auto list = ui::VirtualList<32>(items, ui::Dp{48},
    [](std::uint32_t index) { return index + 1; },
    [this](std::uint32_t key) {
        return ui::Button("Select").on_click([this, key] { select(key); });
    }).grow();
```

`KeyedList<MaxRows>` 用于完整的小列表，`VirtualList<MaxRows>` 只挂载 Host
报告的可见及预取范围。MaxRows 包括预取行；行绑定/回调容量默认从行类型推导，
也可用后两个模板参数指定。更改条数使用 `set_count`，重排或更改 key 映射后调用
`invalidate`。key 必须是可比较、可默认构造、可平凡复制且不超过 16 字节的小值，
数据源应保证全局唯一；SDK 拒绝当前挂载范围内的重复 key，不扫描万条数据。

同一个 key 的行只构造一次，保留行状态与回调；普通内容更新使用行内 State。
回调捕获 key 的值，不引用临时索引。结构提交失败保留旧行和回调，新候选行销毁；
重试成功后才解除离开行的状态订阅。行池为两个 MaxRows 的固定槽，允许当前和
候选行短暂共存，容量与总条数无关。当前行片段不支持嵌套动态模块，返回明确错误；
完整示例见 `examples/virtual-list`。`.grow()` 让列表占据纵向布局剩余高度，
外层布局使用 `.fill().fill_height()`。

运行中的 UI 提交失败会保留页面，并调用可选的
`void on_error(Context&, Error)`；没有钩子时输出错误日志。容量不足等
可恢复错误不会使 Host 停止应用，可以在后续事件重试；协议错误仍返回
失败。首次挂载失败仍属于启动失败，应用不会以空页面继续运行。

`examples/storage` 演示异步读取和持久化。`ctx.storage().get(key, buffer)`
把值复制到调用方缓冲区并返回实际字节数；`set(key, value)` 使用默认 512 字节
封包缓冲区。较大的值可调用 `set(key, value, packet)`；最大 key/value 组合需
2140 字节的复用缓冲区，SDK 直接在其中生成最终协议包，不额外复制大值。协议允许最大
2048 字节，但设备 Host 可以配置更低的单值上限，超过时由 Host 返回错误。

`ctx.fs()` 操作应用私有文件。`open(path, mode)` 返回不可复制、可移动的
`File`；提供 `read`、`write`、`seek` 和显式 `close`，离开作用域也会关闭。
`make_directory`、`remove`、`rename`、`stat` 是异步控制操作。
目录使用 `directory(path)` 打开，再通过 `next()` 获取
`Result<std::optional<DirectoryEntry>>`：空 optional 表示目录结束，错误与
结束分开。目录项拥有最多 64 字节的名称存储，不借用事件内存。

```cpp
auto file = co_await ctx.fs().open("save.bin",
    pxa::OpenMode::write | pxa::OpenMode::create | pxa::OpenMode::truncate);
if (!file) co_return std::unexpected(file.error());
auto written = file->write(bytes);
if (!written) co_return std::unexpected(written.error());
```

FS 的路径在创建任务时校验并编码到有界协程帧，创建任务后原字符串可销毁，
服务入口也可使用 `ctx.fs()` 临时对象。最大路径 rename 直接提交协程内的
538 字节最终包，不要求调整 Context 的默认 scratch。File 必须活到其
异步操作完成或取消；不能对临时 File 启动操作后销毁它。
读写同步返回实际传输字节数，可能短读、短写或 `would_block`；调用方处理
剩余数据并在后续事件重试。示例的四字节保存遇到短写会明确显示失败，
不将已接受的部分写入当作完整保存。完整示例见 `examples/files`。

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

Assets 的 load/query/read 和 FS 路径在任务创建时复制到有界存储，源字符串
可以立即销毁；Assets 直接提交协程内的最终包。读取的输出缓冲区仍为借用。
Clock/Window/Game/Audio/Storage/Permission 的协程按值保存小型服务入口，
`ctx.service()` 临时对象可以立即销毁；Context 必须活到任务完成或取消。
其他异步接口的 `string_view`、`span`、权限及音频等资源对象仍按其接口约定借用，
调用方必须保持它们有效。默认封包与协程帧均有界。

Device 使用 `ctx.device().runtime_info()` 获取目标、架构、引擎、引擎 ABI 与
格式位（Wasm=1、AOT=2）。返回值拥有有界字符串存储，`.target.view()` 等
视图跟随返回对象有效，不引用事件缓冲区。编解码由同一 Device schema 生成，
保持字段顺序、UTF-8 和长度校验。`mac(MacKind, permission)` 返回六字节地址
及 flags，需要 `device.identity` 和对应的精确 scope，例如
`mac.wifi.station.hardware`；Host 可对不支持的 MAC 类别返回 `unsupported`。

Sensor 使用调用方拥有的有界目录缓冲区，不在协程帧中预留完整的 32 项目录：

```cpp
std::array<pxa::SensorDescriptor, 4> sensors;
auto count = co_await ctx.sensors().list(sensors);
if (!count) co_return std::unexpected(count.error());
if (*count == 0) co_return pxa::Result<void>{};
auto permission = co_await ctx.permissions().acquire(
    "sensor.read", sensors[0].semantic.view());
if (!permission) co_return std::unexpected(permission.error());
auto subscription = co_await ctx.sensors().subscribe(
    sensors[0], sensors[0].min_period_ms, *permission);
// 将权限与订阅移动到应用模型，持续接收 on_event；不能在这里就销毁它们。
```

目录字符串拥有存储；解析先验证完整目录及重复 ID/semantic，再修改缓冲区。
容量不足返回 `resource_limit`，输出保持原样。目录 span 必须活到任务完成或取消，
设备不提供物理传感器时，空目录是正常结果。ID 只在本次激活期间有效。
`subscribe` 和 Device `mac` 在调用时保存标量/权限身份，不保存对传入对象的引用；
权限仍需保持有效，Host 会在实际提交和资源使用时验证授权。

`SensorSubscription` 不可复制、可以移动，关闭停止采样；
`subscription.sample(event)` 检查完整 64 位句柄与维数，旧订阅事件返回
`not_found`。当前 Host 每个事件发送一个样本，最多三个维度，值为拥有存储的
`int32_t` 数组；时间戳为单调微秒。按 descriptor 的 unit 解释值，未知 unit
不能猜测缩放。订阅任务取消后，晚到成功句柄自动关闭；stop 阶段不调用 Host。

完整示例见 `examples/device-sensor`。它声明精确的 `ambient.temperature` scope，
只监测目录第一项；适配其他传感器需修改权限声明和选择逻辑。后台关闭订阅，
前台恢复后重新申请；权限撤销取消待处理任务并停止采样。模拟器默认返回空目录；
显式设置 `PXA_SIM_SENSOR_TEMPERATURE_MILLI_CELSIUS=25000` 可提供固定的
25 摄氏度模拟源，它不代表硬件实测。

独立开发包包含 Device schema、生成工具和黄金向量。检查 C++ 生成物：

```sh
python3 spec/draft/tools/generate_service_codecs.py --language cpp --check
```

当前实现包括 Core 消息编解码、资源句柄、应用入口、有界协程和请求表、
声明式布局/常用控件、状态绑定与导航，Storage/Permission/Audio/FS/Device/Sensor，
以及 GameRender 的上下文创建、清屏、矩形和精灵批次 DrawList。
动态 keyed list 与 VirtualList 已有实现和模拟器验证；条件分支、Ref、
其余服务接口和完整性能验收尚未完成。独立开发包已可构建和打包示例，
但目前不能作为完整发布版 SDK。

构建应用时，CMake 中使用 `pxa_add_app`，并提供
`PXA_CPP_SDK_DIR`、`PXA_ARTIFACT_DIR` 和 `PXA_CMAKE_MODULE_DIR`。
`Pxa::Cpp` 包含运行时静态库，头文件中的模板只负责类型化 UI 与任务。
多文件模块使用 `pxa_add_module(model CPP SOURCE_DIRS model INCLUDE_DIRS model)`，
再通过 `pxa_add_app(... CPP MODULES model)` 组合。CPP 选项让模块同时获得
SDK include、C++26 模式和异常/RTTI 配置，不依赖最终可执行文件反向传播编译选项。
完整示例见 `examples/modules`。
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
