# C++26 SDK：服务所有权、编码和接口审查（2026-10-11）

本轮覆盖 Core/协程、资源、文件、Assets、音频、Surface、GameRender、Clock、Sensor、HTTP/IPC，并回归已有 UI、国际化、存储、输入、工作调度和游戏工具。开始时保存了完整 Guest SDK 快照及未提交差异；没有重置原有修改，也没有改动 wire ABI。范围是本轮发现的可复现问题，不表示整个 SDK 已无缺陷。

## 修改与依据

| 修改 | 原因和收益 | 内存与验证 |
| --- | --- | --- |
| Transport 禁止复制/移动 | 内置 span 会指向原对象的 scratch；保持 Context 依赖地址稳定 | 无新字段；C++26 带说明的 delete，编译负例 |
| Task/Yield/TypedIpcCall 标记 nodiscard | 遗漏 co_await/start 会悄悄丢弃懒任务 | 零运行期开销；编译负例；Task 左值消费有明确诊断 |
| Audio graph/open/query/flush 创建时快照 | 原实现延后读取 this、Permission、EqBand；对象移动或输入变化会提交错误请求 | 小型有界请求，固定协程池；移动、输入变更、错误与取消回归 |
| Surface configure/query 创建时记录端点 | 任务延后启动时不再访问已移动的包装对象 | 无引用计数扩展或额外任务；真实 AOT 移动测试 |
| Surface::close() | Host 拒绝关闭时保留资源，供显式重试；成功后使所有像素租约失效 | 现有 control 大小不变；busy/重试/重复关闭测试 |
| 文件/Assets/Sensor/GameRender 返回资源先接管再验证 | 畸形成功响应附有可识别句柄时，旧实现可能先返回错误而泄漏句柄 | 栈上 RAII，无资源回收缓存；四种服务主动响应回归 |
| Clock/Assets/Surface/GameRender 统一 result_body | 拒绝正数、越界负数及带多余数据的错误响应 | 无新常驻数据；合法状态与畸形状态回归 |
| RequestPacket 只复制有效 payload；FS open/stat 预编码提交 | 避免复制整个容量和再次复制到 Transport scratch | 包容量、协程池和请求表不变；字节摘要、导入数、微基准与真实 FS 回归 |
| byte Writer 与 Transport 支持重叠拷贝 | 前向字节循环和先写 header 会破坏输入 | if consteval 保留编译期计算，运行期 memmove；constexpr、重叠与 sanitizer 验证 |
| GameRender Frame 一次成功提交 | 防止重复帧 ID、同一帧再次修改/提交；would_block 保留重试能力 | 复用已有 id/error 字段；模拟 Host 与真实 GameRender 回归 |
| 临时资源描述返回值；限制临时像素租约借用 | 避免引用描述字段或像素时包装对象已析构 | 左值 getter 仍返回 const 引用；编译期类型和误用诊断 |
| HTTP/普通 IPC 在编码前拒绝输入输出重叠 | URL、headers、body 或描述对象可能被前面的字段覆盖 | 不引入大缓冲；保持输入不变的拒绝测试；显式原地 IPC 路径保留 |
| PCM 接受 span<const byte> | 写出数据无需可变缓冲 | IO ABI 不变，内部仅在只读写出边界适配 |

## 接口和迁移

`Transport` 应放在地址稳定的 `Context` 中，或由应用稳定持有并通过引用传递。`Task` 为单一消费者：

```cpp
auto task = context.clock().now();
auto now = co_await std::move(task);
// 或：context.tasks().start(operation()); 检查返回的 Result<void>。
```

小型服务参数在调用时快照；Context、底层资源以及显式借用的输出/外部 packet 仍须在操作完成或取消前有效。任务不延长资源的 Host 生命周期；关闭底层资源后，Host 可以拒绝已经创建的请求。此次修复保证移动包装对象和参数快照不会造成悬空 this，并未引入隐式共享资源或退休队列。

```cpp
std::array bands{pxa::EqBand{1000, -256, 256}};
auto graph = session.graph(-12 * 256, bands);
pxa::AudioSession moved(std::move(session));
// 此后可以改动/销毁 bands，任务仍提交原参数；moved 必须保持打开。
auto applied = co_await std::move(graph);
```

`AudioSession::format()`、`Asset::descriptor()`、`Renderer::info()` 在左值上借用，在右值上返回小型值。`Renderer::frame()` 需要左值 Renderer。保存 `SurfaceFrame` 后再调用 `pixels()/rgb565()`；视图必须在 present、close、reset 或租约销毁前丢弃。

Frame 成功提交后不能重用；为下一帧重新调用 `renderer.frame(buffer)`。`would_block` 可重试同一 Frame；若另一帧已经成功提交，则旧 Frame 被判为 `bad_state`。DrawBuffer 仍由应用独占管理，不能同时交给多个正在编码/等待提交的帧。

HTTP 输入不得借用输出 packet 内存；普通 IPC 编码同样拒绝危险重叠。需要原地 IPC 时使用 `encode_ipc_call_in_place/encode_ipc_reply_in_place` 或类型化 Contract 接口。外部 packet 与输出仍借用至任务完成/取消。

`close()` 是可以检查错误并重试的操作；析构和 `reset()` 是尽力关闭并释放 Guest 所有权，不能在任意 Host 关闭失败后保证回收。应用需要可靠获知关闭失败时必须显式检查 `close()`。

## 验证与数据

原始结果、基准源码、截图与复现步骤见 [Service safety 验证](examples/service-safety/README.zh-CN.md)。使用锁定 WASI SDK 34、O3 和相同的包签名、Host 与应用源码；原生比较交替执行，时序比较固定到同一 CPU。结果不作为 ESP32 FPS 或完整 HTTP/文件系统端到端吞吐量结论。

默认计数器对照：

| 项目 | 修改前 | 修改后 |
| --- | ---: | ---: |
| Wasm | 55,594 B | 55,307 B |
| Linux x86-64 AOT 映射文件 | 86,424 B | 86,200 B |
| ESP32-S3 AOT 映射文件 | 140,460 B | 139,900 B |
| ESP32-S31 AOT 映射文件 | 141,472 B | 140,920 B |
| Guest 线性内存稳定/本次峰值 | 131,072 / 131,072 B | 131,072 / 131,072 B |
| WAMR 稳定/本次峰值 | 31,393 / 48,193 B | 31,393 / 48,193 B |
| event buffer / artifact buffer | 128 / 0 B | 128 / 0 B |
| 原生 Context / TaskScope / 协程池 | 1,736 / 208 / 8,208 B | 1,736 / 208 / 8,208 B |

真实 AOT 的内存比较分别创建全新 Engine，各运行 100 次相同 UI 更新，前后各三次，所有记录相等。WAMR 峰值包含本次初始化，不是历史累计峰值；线性内存是已分配页，不能代替 Guest 分配器内部实际用量或栈高水位。代码映射以各目标 AOT 文件大小另行核对，不与 WAMR 跟踪分配混算。没有扩大任何应用的默认帧缓冲、深度缓冲或命令队列。

FS Guest 微基准包含有效路径检查、请求创建/编码/提交、模拟响应分派、文件资源关闭，前后均 150,000 次、300,000 次导入，字节摘要一致，峰值一个任务槽，池保留 8,208 B。8/64/255 字节路径的中位耗时分别为 391.728→393.503 ns（+0.45%，持平）、807.697→658.751 ns（−18.44%）、2,320.048→2,001.612 ns（−13.73%）。默认计数器相同状态更新/编码中位耗时为 918.977→874.269 ns（−4.87%）；机器上存在其他构建负载，时间差是本机样本，不保证每个平台均同幅提升。完整轮次保存在 JSON；路径长度代表相同的前后工作负载，不能跨路径长度作语言或性能比较。

56 个原生模块/应用回归、额外的 libc++ 诊断二进制、7 项服务编译诊断、8 组 ASan/UBSan、独立 SDK 的 simulator/esp32s3/esp32s31 构建，以及真实 AOT 的 UI、国际化、存储/文件和本轮服务测试均通过。新增服务应用在 240×320/160 DPI、480×640/305 DPI、454×454 圆形/305 DPI 中各重复核心/音频检查两次，并执行前后台切换；三个配置的线性内存均稳定在 131,072 B，WAMR 稳定/本次峰值均为 37,643/58,081 B。这是更广的功能工作负载，不与默认计数器作性能对照。

现有 voxel-craft-cpp、jump-jump-3d-cpp、pixel-dungeon-cpp 使用本轮 SDK 重新构建成功，并以真实 AOT 在 296×240/160 DPI 模拟器启动、渲染、前后台切换；截图逐一复核。这是 API 调整的兼容性与启动画面冒烟验证，不替代三个游戏的完整操作/存档测试，也未测前后游戏 FPS。输入游戏源码中的已有未提交修改保持原样。

## 边界

基线 ASan 进一步确认音频 graph 延后启动会读取已析构的栈上 AudioSession（stack-use-after-scope）；修复后 ASan/UBSan 通过。本轮原生首先复现 16 个失败场景，修复后全部通过，并增加关闭重试、编码重叠和编译误用覆盖。异常 Host 成功响应只有在句柄字段可识别时才能安全回收；不能从任意损坏字节猜测句柄。

pai-touch 已从 `/dev/ttyACM2` 重新枚举至 `/dev/ttyACM1`，MAC A4:CB:8F:D6:8D:A4；运行本轮开始后另一任务烧录的 `efdacc3-dirty` Board 固件。等待该任务完成、确认端口空闲和空间恢复后，在不替换已有应用的前提下安装本轮签名 `service-safety`。296×240 真机三次启动都显示 `Core checks OK`，重复核心检查与音频检查显示 `Audio checks OK`，Home/恢复后状态正确；九张截图已逐一检查。音频权限只授予临时测试包，未修改音量或语言，清理包时撤销其权限和私有数据。

使用设备的 `MEMORY START/STOP` 重置局部最小空闲值，分别测量每次启动与稳定运行阶段，不能混用开机历史峰值。三次服务应用启动：SRAM 稳定增量 676–752 B、本次峰值增量 4,812–4,888 B；PSRAM 稳定增量 578,156–612,444 B、本次峰值增量 835,988–870,244 B。稳定阶段重复核心+音频检查：SRAM 稳定增量均 0 B、本次峰值增量 36–4,136 B；PSRAM 稳定增量均 8 B、本次峰值增量 142,184–143,908 B。不同轮次受首次字体/音频缓存及 Host 状态影响，保留原始样本，不把这组广功能负载与默认计数器比较。

设备日志确认 Guest 线性内存稳定/本次峰值为 131,072/131,072 B；服务应用 AOT 的实际代码映射为 327,680 B，停止后释放。小 Surface 注册 128 B Guest 像素并及时关闭，GameRender 不需要深度缓冲，帧与命令队列在检查后释放；没有把这些临时功能变成所有应用的默认开销。真机测试检查音频提交/渲染成功与零拒绝，不对未进行听音判定的音质作结论。

同一计数器另以临时 ID `pxa-cpp-counter-bench` 编译修改前后版本，避免覆盖已有计数器。顺序为 before/after、after/before、before/after，每次全新启动、点击五次、截图、停止。六张截图均显示计数为 5，未使用屏幕上的整机 FPS 叠层推断应用帧率。六次启动就绪日志的 WAMR current/peak 均为 29,371/29,375 B，linear/linear_peak 均为 131,072/131,072 B，event/artifact 为 128/0 B；这里的 WAMR peak 只覆盖日志打印前的初始化，不冒充后续完整运行峰值。实际 AOT map 均为 131,072 B，停止后释放；UI 在停止时跟踪的 current/本次 peak 均为 1,660/2,508 B。

整机局部堆数据的三次中位数如下，完整原始区间见 `device-counter.json`：

| 阶段/堆 | 前：稳定增量/本次峰值增量 | 后：稳定增量/本次峰值增量 |
| --- | ---: | ---: |
| 启动 SRAM | 1,104 / 4,576 B | 1,140 / 4,576 B |
| 启动 PSRAM | 328,612 / 582,084 B | 328,996 / 582,448 B |
| 五次操作 SRAM | 0 / 124 B | −36 / 36 B |
| 五次操作 PSRAM | 204 / 2,376 B | 200 / 3,748 B |

这些是异步显示、字体、音频、网络和电池等 Host 活动在同一区间的真实整机峰值；负数意味着其他分配被释放。样本有波动，尤其操作阶段的 PSRAM 峰值不能声称完全不增长，也不能据此断言 SDK 增加了常驻内存。能够直接证实不增长的是默认 SDK 对象/池大小、独立 Engine 的稳定/完整本次峰值，以及设备跟踪的 WAMR 启动占用、线性内存、代码映射和 UI 分配。本轮没有改 Host 显示缓存或后台活动，保留这项测量限制。

完成后卸载两个临时包，卸载接口清除它们的私有数据与音频权限，停止局部堆监测并回到启动器。原有五个应用的身份、版本和启用状态一致，138 个已有文件的大小/SHA-256 与设备测试前已保存的快照一致；快照不包含可变安装索引及前一固件任务已更新的三个 voxel 工件。设备多次安装/卸载后 LittleFS 的空间统计暂报 0，清理后重启恢复为测试前同样的 2,215,936 B 可用空间；本轮未修改文件系统或固件。复现脚本 `validation/run_device.py` 已在设备上另外运行一次，通过核心、音频和 Home/恢复检查；截图和清理证据保存在验证目录。

C++26 只使用当前两套编译器真实支持并验证的能力；带说明 delete 已加入配置探测和头文件检查。没有依赖当前 WASI 未提供的 function_ref/inplace_vector，也没有为使用新语法而替换有界池或增加默认容器。
