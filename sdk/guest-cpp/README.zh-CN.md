# PXA C++26 Guest SDK（开发中）

应用接口边界与最近的封装改进见 [API_AUDIT.zh-CN.md](API_AUDIT.zh-CN.md)。

后续新增能力、接口设计和性能优化以 C++ SDK 为主要维护方向。Guest C SDK
逐步退出功能开发，过渡期间保留已有应用必需的兼容与正确性修复。
这不改变 Core wire ABI，也不立即移除旧应用所依赖的接口。

此 SDK 是独立的 C++26 Guest 实现。它直接导入 `pxa_submit` 和 `pxa_io`，
不包含或调用 Guest C SDK。与 Host 的日常调用仍只有这两项导入。
libc 的 `malloc/free` 若已链接，会额外导出供 WAMR 分配配置和事件缓冲，
避免第二套堆覆盖 Guest 对象。构建目标为 `wasm32-wasip1`，需要 WASI SDK 的 libc++。
推荐使用仓库锁定的 WASI SDK 34；异常和 RTTI 默认关闭。
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


自绘游戏可按需包含 `<pxa/ui_display.hpp>`。`ui::decode_start_display(config)`
从 `on_start(Context&, span<const byte>)` 的配置读取屏幕宽高、DPI 密度、
字体缩放、安全边距及四角半径；`ui::decode_display_metrics(event.payload)`
处理 UI 环境变更事件（service 3、opcode 0x8002）。环境几何使用 Surface/Window 坐标；
Canvas 指针坐标使用 dp，叠加自绘 Surface 的游戏须用
`ui::canvas_to_surface_coordinate(value, metrics)` 转换后再命中控件。
`density_q16` 是 DPI 相对 160 的比例，不能替换为 Window 的像素尺寸比例。
缺省圆角扩展按矩形处理；重复、截断及无效尺寸返回协议错误。此可选头文件
只提供小值结构和解码函数，不注册服务、不分配缓存、不增加其他应用的默认开销。

`ctx.window().fullscreen()` 默认将状态栏和导航栏都设为可临时唤出。
游戏可调用 `fullscreen(pxa::WindowBarMode::hidden)` 隐藏状态栏并保留临时导航栏；
第二个参数可独立指定导航栏模式。此选项仍使用同一窗口配置请求，不分配额外状态。
窗口完成挂载及应用恢复前台时可重新申请，避免系统窗口切换后沿用其他页面的配置。
Host 的开发用 FPS 浮层与系统状态栏独立。

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

`TextInput(value)` 在完整系统中点击后会弹出系统输入法。需要主动打开或
关闭时，使用 `<pxa/text_input.hpp>` 的 `TextInputRef`：

```cpp
State<std::string> query{""};
TextInputRef editor;  // 比 Page 活得久
// view 内：TextInput(query, editor).on_submit([this] { search(); })
// 已挂载、前台可见时：
auto opened = editor.show_keyboard();
auto closed = editor.hide_keyboard();
```

`show_keyboard`/`hide_keyboard` 不分配堆内存；未挂载返回 `bad_state`。
Host 必须支持 UI 0.7 的 `text-input-control` 特性，否则返回 `unsupported`。
只有所属应用的可见、启用输入框可以请求打开；切换输入框会取消原焦点。
系统在 UI 轮询中异步显示或收起键盘，当前轮询周期为 200 ms。成功返回表示
焦点请求已接受，不保证该调用返回时键盘动画已完成。
`.on_submit` 在系统输入法确认文本时调用；内容变化仍由 `State<std::string>` 接收。
清空会传递空字符串。需要长输入时使用 UI 0.8 的可选 `dynamic-text` 特性：

```cpp
TextInput(query, editor).max_bytes(1024).single_line().on_submit([this] { search(); })
```

`.max_bytes(n)` 配置 UTF-8 字节上限，字符串与事件按实际内容长度增长，不预留
`n` 字节缓存。`.max_bytes()` 不带参数采用协议上限 4052 字节（一个 4096 字节
Core 1 事件减去消息头）；可设置 1..4052。超限按完整 UTF-8 字符边界截取。
搜索和网址使用 `.single_line()`，以横向滚动避免长网址反复折行；
需要多行时保留默认模式或使用 `.single_line(false)`。输入框高度保持应用设定值。
组件清单应声明 UI 的 `dynamic-text` 特性，旧 Host 会在加载时报告不支持。
未使用 `.max_bytes` 的旧输入框保留 64 字节行为，普通 `TextInput(value)`
对象仍只有一个指针大小。ESP 队列中不超过 64 字节的文本继续内联保存；长文本
按实际长度分配，事件处理、过期丢弃或入队失败都会释放，队列槽位大小不变。
输入框卸载、删除或退到后台后，系统会收起键盘。

键盘复用系统拼音词典、中文/英文/数字/符号模式、字号与主题色。应用不会复制
词典，也不能通过本接口覆盖系统键盘配色。系统浅色/深色或主色改变时，已打开
的键盘、候选面板与符号页会一起更新并保留输入内容；阅读器自身的纸张配色
可以与系统主题独立。`examples/system-input` 演示打开、关闭和提交。
独立 `product` 模拟器没有系统输入法层，应使用完整 `ui` 模拟器或真机验证。

运行中的 UI 提交失败会保留页面，并调用可选的
`void on_error(Context&, Error)`；没有钩子时输出错误日志。容量不足等
可恢复错误不会使 Host 停止应用，可以在后续事件重试；协议错误仍返回
失败。首次挂载失败仍属于启动失败，应用不会以空页面继续运行。

`examples/storage` 演示异步读取和持久化。`ctx.storage().get(key, buffer)`
把值复制到调用方缓冲区并返回实际字节数；`set(key, value)` 使用默认 512 字节
封包缓冲区。较大的值可调用 `set(key, value, packet)`；最大 key/value 组合需
2140 字节的复用缓冲区，SDK 直接在其中生成最终协议包，不额外复制大值。协议允许最大
2048 字节，但设备 Host 可以配置更低的单值上限，超过时由 Host 返回错误。

保存整数、布尔值或 IEEE 浮点数可直接使用类型化入口：

```cpp
auto count = co_await ctx.storage().get_value<std::int32_t>("count");
auto saved = co_await ctx.storage().set_value("count", std::int32_t{42});
```

这些入口使用明确的小端格式，拒绝字节数不符及非 0/1 的布尔值；不会序列化
C++ 结构体内存布局。键和值在创建任务时已经复制，临时字符串及服务入口可
立即销毁，Context 须保持有效。四字节值最多预留 96 字节最终封包，使用一个
服务协程，不预留通用 `set` 的 512 字节封包容量。原有字节接口仍适用于
自定义版本化存档，现在也在创建任务时复制键和值，只借用 output 和外部 packet。
外部 packet 必须活到完成或取消，不能给多个尚未完成的请求复用；默认字节
`set` 的最终封包仍最多 512 B，但只用一个服务协程且只复制实际载荷。
无效参数直接返回失败任务，不占用协程槽。`list(after, output)` 校验返回键严格
递增且大于 after；容量或协议错误不会覆盖旧输出。

应用自有的文件、Work input 等数据可按需包含 `<pxa/binary.hpp>`，使用
`binary::encode(value)`、`decode<T>(bytes)` 或有界的 `Reader`/`Writer`。
读写均返回 `Result`，容量不足不会推进位置，支持显式小端整数、float/double
和规范布尔值；`Reader::finish()` 检查尾部多余数据。它不负责版本、校验和或
业务字段校验。PXA 服务编码保留在 SDK 的 `wire` 内部，普通应用不需要拼包。

服务结果与生成 IPC 结构的文本类型为 `pxa::FixedText<N>`，位于 `<pxa/text.hpp>`。
这是拥有存储、无堆分配的有界 UTF-8 文本；非空文本赋值超过容量或非法 UTF-8
会失败并保留原值。`wire::OwnedText<N>` 保留为同一类型的兼容别名。

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
Permission 的 name/scope 也在创建任务时编码；默认 check/acquire 不再嵌套
第二个服务协程，512 B 默认封包容量保持不变。外部 packet 仍借用到完成或取消。
Storage/Permission 的默认拥有式入口使用新鲜封包，不暂存另一份键或名称；
外部封包入口另行处理输入重叠。
其他异步接口的 `string_view`、`span`、权限及音频等资源对象仍按其接口约定借用，
调用方必须保持它们有效。默认封包与协程帧均有界。

按需包含 `<pxa/events.hpp>` 可写 `event.is<pxa::SensorSample>()`、
`event.is<pxa::PermissionRevoked>()` 等，无需填写服务号和操作码。
支持 SensorSample、PermissionRevoked、PlaybackEvent、SurfaceRelease、
WorkStopRequested、IpcRequest/TypedIpcRequest 和 ui::CanvasPointer。
它只匹配消息类型；即使 token/载荷损坏也会匹配，随后必须调用对应解码器
并传播错误。IPC 还需匹配 endpoint/契约，Canvas/资源还需匹配页面代次/句柄。
没有额外订阅表、协程、缓存或 Event 字段。完整示例见 device-sensor、ipc-stats、work。

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
`examples/work` 是 UI 与 job 两个独立 Component 的打包示例。UI 先在 Storage
持久化业务动作序号，再将它放入 input；job 用该序号保存幂等标记，按重试上限
返回 retry/failure。Host Work ID 只用于管理队列，空队列重新加载后可能重用，
不能作为永久业务幂等键。产品模拟器与 ESP Host 均已接入持久化队列；跨启动的
单调时间无法比较，恢复任务会重新到期，业务必须允许重复执行。

Surface 的 RGB565 映射模式使用 `ctx.surface().create_mapped(options)` 创建。
异步创建按值持有服务入口，可先销毁临时 facade 再启动任务；Context 和已创建
Surface 仍须覆盖其请求及帧租约的使用期。
SDK 按 Host 返回的 stride 和 frame bytes 一次分配 64 字节对齐的 Guest 缓冲区，
注册后由 `Surface` 持有；移动 Surface 不会改变缓冲区地址。`acquire()` 返回不可
复制的 `SurfaceFrame`，`pixels()` 是当前帧的可写视图，`present(frame_id)` 将帧
交给 Host。没有空闲缓冲区时返回 `would_block`，应等后续帧事件再试。
丢弃尚未 present 的帧会关闭 Surface，防止缓冲区永久被占用。Surface 关闭后
仍存活的帧租约不再暴露像素；所有租约销毁后才释放 Guest 缓冲区。
`decode_surface_release(event)` 解码 Host 的释放通知，`query_state()` 返回实际
提交、显示、丢弃和空闲缓冲区计数。映射避免每帧通过 ABI 复制像素，不保证
Host 合成和显示链路零拷贝。

绘制映射像素时，`frame.rgb565()` 提供借用的类型化像素视图：

```cpp
auto pixels = frame.rgb565();
if (!pixels) return;
auto row = pixels->row(y); // 每行检查 y 和实际 stride。
if (row) (*row)[x] = 0x07e0; // x 须小于 row->size()，与 span 索引约定一致。
```

视图按 RGB565 小端格式读写，支持填充、未对齐地址和有 padding 的 stride，
不需要应用调用 `wire::put16`，也不增加帧缓冲或 Surface 常驻字段。视图和行
均不延长帧租约，present、关闭或销毁帧之后必须丢弃它们。

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
Canvas 直接绘制与拥有数据的触摸输入、位图协程池、资源绑定和 3D 裁剪已补齐。
15 个示例提供 Wasm 与 Linux/S3/S31 构建；当前仍是开发包，支持范围、设备验证和
性能数据见 `VALIDATION.zh-CN.md`。S31 本轮只做交叉构建；未验证的设备与显示组合
不能作为完整发布版的承诺。

若游戏和 HUD 都绘制在 Surface 中，可设置 `RenderOptions::direct_scanout=true`
并使用输入专用 Canvas，让 Host 选择直接显示。该选项是偏好，系统覆盖层及
可见 UI 仍由 Host 决定合成；默认关闭，不改变普通 UI 应用的显示行为。

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

`solid_depth_quad(vertices, color)` 直接提交 RGB565 深度四边形，不需要纹理绑定。
第三个参数 `true` 使用扫描线深度路径，需要 painter-depth 能力和已绑定的
lit palette；颜色直接使用 RGB565，透明面应继续使用纹理。扫描线纹理的
`affine_uv` 只改变 UV 插值，仍使用完整的逆深度检测。

`textured_depth_polygon(binding, perimeter, options)` 和
`solid_depth_polygon(perimeter, color)` 接受 3–10 个凸周界顶点，将裁剪后的
面编码为同一深度扫描线内核的四边形或退化四边形扇形。量化后零面积扇面
被跳过；先检查整个面的命令/字节预算，容量不足不写入半个面。
需要 painter-depth 能力及已绑定的 lit palette，不重排透明面，也不分配缓存。

游戏辅助模块按需包含，不增加所有应用的默认存储：

慢帧游戏可以显式声明 `static constexpr pxa::game::LoopOptions loop_options`。
默认为 16 ms tick、16 ms 模拟步长、最多 4 次更新；Voxel 使用最多 8 次，
使约 10 FPS 的帧保留六个模拟步，避免每帧丢失约三分之一移动/重力时间。
后台恢复仍重置时钟，超过预算的长暂停仍有界；常量配置不增加运行时存储。

- `game3d_camera.hpp` 的 `CameraBasis::from_pose(eye, yaw, pitch)` 一次准备
  相机基向量，`to_view` 重用它；世界 Y 向上、屏幕 Y 向下，正 pitch 向下。
  每个显式持有的基向量占 48 字节，不缓存全场景的顶点。
- `game3d_material.hpp` 的 `choose_material_path(vertices, max_uv_texels,
  cutout, policy)` 按屏幕尺寸与 Q8 深度范围选择透视、仿射或纯色。
  `max_uv_texels` 包括合并面的重复纹理跨度；仿射判据包含深度量化余量，
  将 UV 偏差限制在约半个纹素。纯色会丢失微小面的纹理细节，只用于不透明面；
  `MaterialPolicy{.solid_extent_q4=0}` 可禁用这种画质取舍。树叶孔洞继续采样纹理。
- `game_utils.hpp` 的 `RecyclingPool<T,N>` 使用显式固定容量，`acquire()`
  清空并循环复用最旧槽。大小为 N 个 T 加游标及对齐；无默认全局池，
  调用方不能把槽引用保留超过 N 次复用。`StageStatistics<N>` 只保存
  N 个 24 字节计数器，不自行调用时钟或输出日志；窗口之间显式 `reset()`。
- `profile_clock.hpp` 提供可选的 WASI 单调时钟采样。短区间工具使用低 32 位
  纳秒，单个区间必须短于 4.29 秒。时钟调用本身有成本，应在阶段边界采样，
  不要逐面、逐像素计时；异步提交的墙钟时间还可能包含 Host 任务抢占。

`Projector::clip_range(near, far)` 更新裁剪距离而不重算 FOV；
`focal_length()` 供同一投影的区块可见性判定使用。裁剪采用两个固定容量数组，
每个平面后交换输入输出，仅复制构造实际使用的顶点，避免默认成员初始化
清写未使用的存储。两块栈存储仍合计 480 字节，不分配临时容器。
`project_polygon` 返回凸多边形周界，
需要三角形的调用方只做一次扇形展开；`project_triangle` 已返回连续三角形。

`textured_quad` 和普通 triangle batch 使用深度缓冲，创建上下文时应设置
`RenderOptions::scratch = Scratch::depth16`；其额外存储约为渲染宽×高×2 字节，
不应为只有 sprite 的游戏默认启用。`AtlasBinding` 是纹理槽号，不拥有资源。
用 `ctx.assets().load(AssetKind::texture/palette, path)` 按需获取资源，在场景切换时
用 `renderer.bind_assets(...)` 批量绑定，或用 `renderer.bind_asset(asset, {slot})`
逐个绑定。Host 的 Renderer 持有绑定资源的引用，成功绑定后可以销毁临时
`Asset`，不会丢失纹理；这样加载 48 个槽也无需同时占用 48 个 Guest 句柄。
替换绑定、关闭 Renderer 或 Host 撤销资源时释放相应引用；帧循环不读取或
解码文件。透明图元仍按调用顺序编码，SDK 不跨透明边界重排批次。
`examples/game` 从 PNG 与共享调色板编译包内 PXR，首次进入前台时加载并绑定，
随后使用深度 scratch 绘制运动的纹理四边形和三角形。加载失败会终止本次初始化；
资产句柄由 `Game` 保存，帧循环不访问文件。其他纹理选项需按 Host 返回的能力和
所选 scratch 模式使用。

可选的 `<pxa/game3d.hpp>` 接受相机空间的 `MeshVertex`，对三角形做近/远与屏幕
视锥裁剪及背面剔除，写入调用方提供的 `std::span<game::Vertex>`。`Projector`
在初始化时按渲染分辨率和视角创建；单个三角形裁剪后最多需要 21 个输出顶点。
坐标的 `z` 向屏幕内增大，输出深度为 Q8 距离；可选 `Transform::rotation_y()`
用于模型旋转。`project_triangle()` 不分配内存，返回的顶点可直接交给
`Frame::solid_triangles()` 或 `Frame::triangles()`，不能再将结果当作周界扇形
三角化。`project_polygon()` 接受凸三角形或四边形，返回裁剪后的周界，最多
10 个顶点并插值 UV；只有周界结果需要调用方三角化。完全位于视锥内的面
直接投影，不构造或复制中间裁剪数组。屏幕覆盖的 UI/HUD 可以用普通 UI 组件，
也可以在世界之后追加到同一 Surface 的 2D 绘制列表；不要混入世界的深度批次。
完全自绘的 HUD 可用 `sprites()`，或给 `textured_quad()` 设置
`{.affine_uv=true, .painter=true, .transparent_index0=true}`，并在顶点明确指定
palette 光照行（例如 0）。这复用 2D 扫描线内核，不检测或写入深度；
`painter=true` 同时设置 `lit_palette=true` 则选择带深度路径，适合世界几何。
顺序覆盖的 HUD 无需增加全屏图层或合成缓冲；按钮和字体本身的光栅化仍有成本。

Canvas 直接绘制位于 `<pxa/canvas.hpp>`：应用持有 `ui::CanvasRef` 和复用的
`ui::CanvasCommands<N>`，用 `Canvas(ref, width, height)` 挂载（零宽高表示填满）。
命令支持矩形、圆角、线、文字和 clip/pop；`ref.present(commands)` 执行有界的
BEGIN/WRITE/PRESENT，WRITE 直接使用命令缓冲中的最终协议包，没有第二份
命令数组。容量不足、未闭合 clip、未挂载或已停止返回错误，不分配备用缓冲；
当前直接控制包容量最多 4064 字节，不支持超出容量后的 StreamIO 分片。
CanvasRef 必须比页面活得久，挂载成功才生效，卸载自动失效。
完整绘制和点击示例见 `examples/canvas`。

`Canvas().input_only().on_pointer(callback)` 只作为游戏输入层，根背景透明，
不申请 Canvas 绘图缓冲或全屏 alpha 平面。回调收到拥有标量数据的
`CanvasPointer`，坐标有符号，phase 为 down/move/up/cancel，包含 pointer_id；
应用应按 ID 分别管理手指，处理 cancel，并在后台清除持续操作。

有界任务池通过 `task_pool_stats()` 报告预留字节、槽大小、当前/峰值槽数和
分配失败数。`PXA_COROUTINE_SLOT_BYTES` 默认 1024，`PXA_COROUTINE_SLOT_COUNT`
默认 8；`PXA_REQUEST_CAPACITY` 默认 16（范围 1..16），`PXA_TASK_SCOPE_CAPACITY`
默认等于协程槽数。使用 CMake cache 定义这些容量，`PxaGuest.cmake` 将同一值
传播到 SDK 和所有 Component，不能只在单个源文件中定义导致对象布局不同。
默认池的统计及占用位图合计为 8208 字节；默认原生 Context 从 2120 减为
1736 字节。实际 WASI 栈/线性内存、Host 缓存和 AOT 映射仍需分别测量。

游戏若需要 HUD，可让 `view()` 返回 `ui::Overlay(Column(...))`。它保持根节点
透明且不拦截空白处的触摸，将内容标记为独立的 LVGL alpha 层；文字、按钮等
子节点继续由 UI 服务管理。黑色游戏背景上可用 `Text(...).rgba(0xffffffff)`
指定白色文字，参数格式为 `0xRRGGBBAA`。HUD 状态
通过 `State<T>` 更新即可，只有值变化时才提交 UI 补丁。不要在每帧写入不变
的 HUD 文本。`examples/game` 每 60 次固定更新改变一次计分，移动纹理与 3D
三角形仍独立提交到 GameRender。

alpha 层按内容边界缓存，只在 UI 属性变化时重新生成；设备仍需在每个可见帧
合成该区域，并从 Surface 直接扫描切换到 LVGL 合成路径。对于持续高帧率的
游戏，应把常驻计分、准星等优先画进 GameRender；仅在需要原生控件或系统
交互时启用 LVGL 弹层。当前系统 toast、授权框等宿主弹层尚未纳入这个 alpha
平面，不能假定它们会正确叠加在游戏 Surface 上。

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

发布构建默认删除 Wasm 的 DWARF 调试段，保留函数名与执行段；预编译 WASI
libc 也可能带有大量 DWARF。需要保留时设置 `PXA_KEEP_WASM_DEBUG=ON`
（CMake cache 或打包环境变量）；CMake `Debug` 构建自动保留。该选择不改变
Guest 线性内存、AOT 执行代码或运行时缓存。

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

自绘游戏可选用 `<pxa/game_pacing.hpp>` 的 `can_build_frame(telemetry, limit)`，
在几何构造之前检查尚未退休的绘制列表。默认 limit=2 允许一份正在光栅化、一份
待处理；limit=1 保留 LCD 传输与下一帧构造的重叠，减少 Guest 和光栅化同时访问
外部内存的压力。调用方决定查询时机与失败策略；此头文件不增加队列或常驻状态。
背压只跳过绘制，固定步长更新和输入仍应继续；FPS 和自适应视距需累计跳过的
绘制 tick 时间，不能仅用提交成功的 tick 间隔。实际显示速度应由 Host 完成计数验证。

`ctx.fs().replace(temporary, destination)`（FS 0.2）可原子发布已关闭的普通文件，
用于书架、存档或下载缓存。失败保留原目标；不替换目录、链接或打开的文件。
`rename` 仍要求目标不存在。先完成写入、调用 `File::close()`，再 `co_await replace`；
不要用“删除旧文件再重命名”模拟原子保存。Host 不支持时返回 `unsupported`。

国际化可选模块支持静态翻译目录、类型化具名参数、复数、数字格式、动态字符串与固定缓冲区、系统语言切换及 UI 绑定：[国际化 SDK](I18N.zh-CN.md)。
