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
候选行短暂共存，容量与总条数无关。行片段可以包含嵌套动态模块；模块与列表行
共享同一事务和节点 ID 分配器，失败时逐层回滚，离开行时解除订阅。
完整示例见 `examples/virtual-list`。`.grow()` 让列表占据纵向布局剩余高度，
外层布局使用 `.fill().fill_height()`。

条件内容用 `When(condition, then_factory, else_factory)`，两个工厂返回不同类型
的声明式视图，只有当前分支会被构造和挂载：

```cpp
State<bool> expanded{false};
auto view() {
    return Column(
        Toggle("Details", expanded),
        When(expanded,
             [] { return Text("Visible details"); },
             [] { return Text("Collapsed"); })
    );
}
```

同一事件内状态多次变化只按最终分支提交；若事务失败，原分支、回调与订阅保持
有效，下次事件可重试。`When` 可放在列表行中，分支内也可包含列表。切换成功
后旧分支销毁，旧事件随页面 generation 失效；要保留业务状态应放在分支外。

需要命令式更新某个已挂载控件时，可使用 `Ref<bool>`、`Ref<int>` 或
`Ref<std::string>`。目前支持 Text、Toggle、Slider、Progress 和 TextInput：

```cpp
Ref<int> score{0};
auto view() { return Text(score); }
// 仅在节点成功挂载期间调用：
auto updated = score.update([](int value) { return value + 1; });
```

`Ref` 不要求用户分配节点号；页面负责绑定和解除绑定。`set`/`update` 在未挂载时
返回 `bad_state`，提交失败时仍指向旧节点，成功移除节点后立即失效。
同一视图片段不能把一个 `Ref` 挂载到两个节点。`Ref` 必须比引用它的页面存活更久；
要在隐藏页面期间修改共享数据，应使用 `State<T>` 作为应用模型。

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

Net 通过 `ctx.net().request(request, permission, packet, headers)` 发起 HTTP 请求。
先用 `ctx.permissions().acquire("net.client", origin)` 获取与 URL origin 完全一致的
权限，例如 `https://example.test`。请求在调用 `request()` 时立即编码，URL、
请求头和小型请求体只需活到该调用返回；控制包与响应头数组由调用方持有，
必须活到异步任务完成或取消。控制包上限 4096 字节，不放进默认 1024 字节
协程槽。响应头数组只需容纳实际返回的所选头；不足时返回 `resource_limit`，
已交付的响应流也会关闭。响应中的头字段是拥有存储的副本。

```cpp
std::array<std::byte, 4096> packet;
std::array<pxa::NetHeader, 2> headers;
constexpr std::array<std::string_view, 2> wanted{"etag", "content-length"};
pxa::NetRequest request{.url = "https://example.test/data",
                        .wanted_headers = wanted};
auto reply = co_await ctx.net().request(request, permission, packet, headers);
if (!reply) co_return std::unexpected(reply.error());
if (reply->body) {
    std::array<std::byte, 512> chunk;
    auto count = reply->body.read(chunk);
    // 短读与 would_block 由调用方在后续事件继续处理。
}
```

HTTP 404 等状态码是有效的 `NetResponse`，不是 PXA 错误。没有响应体时 `body`
为空；有响应体时 `NetBody` 不可复制、可以移动，离开作用域自动关闭。
`body_length_known()` 为假时不能用 `body_length` 判断结束，`read` 返回 0 才是 EOF。
请求取消后晚到的响应体句柄由请求表关闭；stop 阶段不调用 Host。

IPC 范围是同一 App 的 Component 通信。`examples/ipc-stats` 展示类型化契约：
`stats.contract.json` 声明 endpoint、版本、严格递增的字段编号与字段类型；
`pxa_add_ipc_contract` 在构建时生成双方共用的 C++ 编解码头文件：

```cmake
pxa_add_ipc_contract(stats_contract CONTRACT stats.contract.json
                     OUTPUT stats_generated.hpp)
pxa_add_app(pxa_main COMPONENT_ID main SOURCES main.cpp
            LIBRARIES stats_contract)
```

```cpp
stats::Get::Request request{};
request.seed = 41;
request.label.set("test");
pxa::IpcCallBuffers<stats::Get> buffers;
auto result = co_await ctx.ipc().call<stats::Get>(request, buffers);
```

提供方在 `on_event` 中调用 `decode_ipc_request<stats::Get>(event)`，取得有界拥有存储
的请求，再用 `ctx.ipc().reply<stats::Get>(call_id, response, reply_buffer)` 回复。
当前生成器支持 `u32`、`i32`、`bool`、有界 UTF-8 `text` 字段。可选字段写
`"optional": true`，只允许追加在必需字段后，生成 `std::optional<T>`；未赋值
时不编码，编码时按 ABI 设置字段号最高位。解码器接受缺失的可选字段及更高
编号的未知可选尾部字段，拒绝未知必需字段、乱序、重复、截断、无效文字和超
预算消息。字段按编号逐条编码，不序列化 C++ 对象
布局。兼容性追加可提升 minor，改变既有字段或语义须更换 endpoint 的 major
版本。跨 App 路由尚未支持。
类型化接口直接写入调用方包缓冲区，不复制整份 payload。生成类型公开请求、
回复与结果的最大字节数；`IpcCallBuffers` 和 `IpcReplyBuffer` 按此预留固定容量，
必须活到任务完成或取消。回复的文本值复制进生成类型的固定容量存储。

仍可使用有界的原始消息接口。
`ctx.ipc().call(endpoint, payload, packet, output)` 在调用时把 endpoint 和
payload 编码进调用方控制包；包与 output 必须活到任务完成或取消。
调用先等待 Core 接受，再用独立的 IPC call ID 等待最终结果；call ID 与 Core
请求 token 即使数值相同也不会混淆。成功返回的 `IpcCallResult` 含 call ID 和
实际回复字节数，回复内容写入 output；容量不足返回 `resource_limit`。
提供方在 `on_event` 中用 `decode_ipc_request(event)` 取得仅在当前事件有效的
endpoint/payload 视图，然后调用 `ctx.ipc().reply(call_id, status, payload, packet)`。
若要在事件返回后才回复，必须先复制请求数据。取消等待中的调用不会撤销已被
Broker 接受的工作；后续结果会被忽略，提供方仍应回复或由 Host 停止流程清理。
独立 service Component 的完整示例见 `examples/ipc-stats`。

Work 通过 `ctx.work().enqueue({.worker = "sync.job", ...})` 交给单独声明的
`job` Component。`enqueue` 返回 Work ID 与 Host 授予的执行窗口，不表示
job 已启动；`cancel(id)` 与 job 内的 `complete(id, WorkResult::success/retry/failure)`
分别返回操作完成状态。请求在创建任务时编码进有界协程帧，短期 worker 字符串
和最多 24 字节 input 可立即销毁。job 的 `on_start(Context&, config)` 使用
`decode_work_start(config)` 获取拥有存储的 ID、attempt、deadline 和 input；
`on_event` 可用 `decode_work_stop_requested(event)` 响应停止请求。
`examples/work` 是 UI 与 job 两个独立 Component 的打包示例，使用业务键保存
幂等标记，并按重试上限返回 retry/failure。产品模拟器已注册 Work 服务，
可运行这个双 Component 示例；模拟器当前使用进程内有界队列，重启后不恢复
待执行项。ESP 产品 Host 尚未接入 Work，不能把模拟器结果视为实机验收。

Surface 的 RGB565 映射模式使用 `ctx.surface().create_mapped(options)` 创建。
SDK 按 Host 返回的 stride 和 frame bytes 一次分配 64 字节对齐的 Guest 缓冲区，
注册后由 `Surface` 持有；移动 Surface 不会改变缓冲区地址。`acquire()` 返回不可
复制的 `SurfaceFrame`，`pixels()` 是当前帧的可写视图，`present(frame_id)` 将帧
交给 Host。没有空闲缓冲区时返回 `would_block`，应等后续帧事件再试。
丢弃尚未 present 的帧会关闭 Surface，防止缓冲区永久被占用。Surface 关闭后
仍存活的帧租约不再暴露像素；所有租约销毁后才释放 Guest 缓冲区。
`decode_surface_release(event)` 解码 Host 的释放通知，`query_state()` 返回实际
提交、显示、丢弃和空闲缓冲区计数。映射避免每帧通过 ABI 复制像素，不保证
Host 合成和显示链路零拷贝。

映射需要签名的 pinned-memory 声明。CMake 应用在 `package.json` 的 `build`
中声明 `linear_memory.maximum_bytes`（64 KiB 整页）和 `pinned: true`；打包工具
将上限传给 Wasm 链接器，验证生成的内存最大值，并写入签名 manifest。该上限
约束整个 Component 的线性内存，应计入双/三帧缓冲、协程池和应用数据。
完整示例见 `examples/surface`；未支持固定内存地址的 Host 会拒绝缓冲区注册。
Window 快照可能在 Guest `on_start` 之后才由 Host 发布；需要屏幕尺寸的游戏或
Surface 应在首次 `on_foreground` 中启动初始化任务，后台恢复时避免重复创建。

独立开发包包含 Device schema、生成工具和黄金向量。检查 C++ 生成物：

```sh
python3 spec/draft/tools/generate_service_codecs.py --language cpp --check
```

当前实现包括 Core 消息编解码、资源句柄、应用入口、有界协程和请求表、
声明式布局/常用控件、状态绑定与导航，Storage/Permission/Audio/FS/Device/Sensor/Net/IPC/Work/Surface，
以及 GameRender 的上下文创建、清屏、矩形、精灵批次、纹理四边形和三角形批次 DrawList。
动态 keyed list、VirtualList、条件分支与常用控件 Ref 已有实现；
其余服务能力和完整性能验收尚未完成。独立开发包已可构建和打包示例，
但目前不能作为完整发布版 SDK。

GameRender 帧使用调用方拥有的 `DrawBuffer<N>`，在每次 `frame(buffer)` 后按
绘制顺序编码，`submit()` 一次调用提交；超出容量或缺少能力会返回错误，不
分配备用堆缓冲。`SpriteOptions` 和 `PolygonOptions` 使用具名字段，
`Renderer::supports(RenderCapability::triangle_batch)` 可在绘制前检查能力。
`Vertex` 的屏幕坐标与 UV 为 Q4，默认 `light=255` 表示纹理全亮、
`depth_q8=256` 表示深度 1。lit palette 与 painter 使用者应明确设置光照行；
普通图元的深度必须非零，painter triangle 的深度必须为零。
三角形顶点按每组三个连续排列。

```cpp
auto frame = renderer.frame(commands);
frame.clear({0x0000});
frame.sprites({0}, sprites, {.transparent_index0 = true});
frame.textured_quad({0}, quad_vertices, {.affine_uv = true});
frame.solid_triangles(triangle_vertices, {0xf800});
auto result = frame.submit();
```

`textured_quad` 和普通 triangle batch 使用深度缓冲，创建上下文时应设置
`RenderOptions::scratch = Scratch::depth16`；其额外存储约为渲染宽×高×2 字节，
不应为只有 sprite 的游戏默认启用。`AtlasBinding` 是纹理槽号，不拥有资源。
用 `ctx.assets().load(AssetKind::texture/palette, path)` 按需获取资源，在场景切换时
用 `renderer.bind_assets(...)` 绑定，保持 `Asset` 存活到不再使用该槽；帧循环
不读取或解码文件。透明图元仍按调用顺序编码，SDK 不跨透明边界重排批次。
`examples/game` 使用深度 scratch 绘制运动的矩形和三角形；其他纹理选项需按
Host 返回的能力和所选 scratch 模式使用。

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

打包器默认最多使用 8 个主机编译任务；设置 `PXA_BUILD_JOBS=4` 可按机器内存
调整，`PXA_BUILD_JOBS=1` 可复现串行构建。CMake 项目有 Ninja 时默认使用
Ninja，也可用 `CMAKE_GENERATOR='Unix Makefiles'` 指定生成器。独立 Component
的 AOT 编译按同一任务上限并发。这些设置只影响主机打包过程，不启用 Guest 线程。

主仓库的 `tools/app.sh build` 默认将 CMake 中间文件缓存于
`local/app-build-cache`，重复构建仅重新编译变化的源码。独立打包器可设置
`PXA_BUILD_CACHE_DIR` 指定持久缓存目录；不设置时使用临时构建目录。

开发包自带构建时所用的 `wamrc`，因此当前预编译包仅适用于相同的 Linux
主机架构；其他主机可以通过 `WAMRC` 指定匹配的编译器。WASI SDK 34 可由
打包脚本按锁定摘要下载，也可显式设置 `WASI_SDK_DIR`。发布时先更新 PXA
仓库根目录的 `VERSION`，开发包直接复制该值，不维护独立 SDK 版本号。
WASI SDK 34 链接 libc++ 后，当前最小应用也会导入 `clock_time_get`；
清单需要声明 `monotonic-clock` 和 `wall-clock`，打包校验会检查实际导入。
