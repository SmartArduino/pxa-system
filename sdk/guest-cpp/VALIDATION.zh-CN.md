# C++ Guest SDK 验证记录

状态：开发中，尚未达到 `local/PLAN.md` 的完整交付条件。
以下为 2026-09-29 至 2026-09-30 本地执行得到的阶段证据，不代表完整 SDK 验收。

## 已执行

- `bash tools/package/test_guest_cpp.sh`：23 组 C++26 主机测试通过，覆盖特性、Core、
  UI 编码/控件/状态/导航、生命周期、有界任务、Assets、Storage、FS、Permission、
  Audio、Device、Sensor、Net、IPC、Work、GameRender 创建/绘制/帧调度。公共运行时目标文件
  各编译一次后供测试链接，不改变测试的源代码编译选项。
- 当前工具链 CMake 真实编译探测确认 C++26 模式、显式对象参数、包索引、
  expected/span/协程头文件可用；WASI libc++ 的 inplace_vector/function_ref
  编译探测失败，它们为可选能力，Guest 不依赖。主机为 Clang 22.1.8/
  libstdc++ 16，两项可选设施通过运行测试。C++23 编译公共头文件按预期报错。
- 动态列表测试覆盖稳定 key 重排复用、重复 key、结构提交失败保留回调、
  大范围跳转、旧事件拒绝、容量耗尽、行 State 的失败重试及离开行解除订阅；
  这些路径不触发 Guest 堆分配。局部字符数组副本和长只读文字描述挂载也零分配。
- 条件分支定向测试覆盖事务失败后旧回调仍可用、成功后旧事件拒绝、同周期
  状态恢复不提交、`When` 嵌套在 keyed list 行内及分支内嵌 keyed list。
  新行的嵌套模块只准备一次；定向测试的 ASan/UBSan、use-after-scope 和
  detect_stack_use_after_return=1 通过，挂载、切换和回滚期间无 Guest 堆分配。
- `Ref<bool>`/`Ref<int>`/`Ref<std::string>` 的页面容量推导、挂载后更新、提交失败保留
  原引用、成功隐藏后失效、重挂载保留值，以及同页重复引用拒绝已通过定向测试。
  完整 23 组主机测试再次通过；页面和动态分支定向测试的 ASan/UBSan、
  use-after-scope、detect_stack_use_after_return=1 通过。使用锁定 WASI SDK 34
  构建 `conditional` 的 Linux x86_64、ESP32-S3、ESP32-S31 Wasm/AOT 包，产物分别
  在 `/tmp/pxa-cpp-ref-pkgs{,-esp32s3,-esp32s31}/pxa-conditional.pxa`。
  导航测试还覆盖新旧页面间共享 Ref 的所有权转移，失败时仍保留旧页。
  Toggle、Slider、Progress、TextInput 使用同一绑定实现，控件定向测试覆盖
  程序更新、输入事件与页面卸载。产品模拟器实测隐藏、显示 0、点击显示 1、隐藏后重显 1；截图为
  `/tmp/pxa-cpp-ref-{initial,shown,updated,hidden,restored}.png`。新 SDK 包
  `/tmp/pxa-cpp-ref-sdk-20260930` 在仓库外 `/tmp` 使用包内源码、CMake 和工具
  构建并打包相同示例，输出 `/tmp/pxa-cpp-ref-external/pxa-conditional.pxa`。
  最终 `RefStorage<0>` 使用空存储，未使用 Ref 的页面对象维持 312 字节，
  显式设为零容量时返回 `resource_limit`；主机套件及定向 ASan 再次通过。
  `settings` 将 Toggle、TextInput 切到 Ref 后，使用最终源码生成 Linux x86_64、
  ESP32-S3、ESP32-S31 Wasm/AOT 包，位于
  `/tmp/pxa-cpp-ref-final-settings-{simulator,esp32s3,esp32s31}/pxa-settings.pxa`。
  产品模拟器实际点击 Sound 开关，截图
  `/tmp/pxa-cpp-ref-settings-{before,after}.png` 显示由开转关，正常退出。
  最终独立包 `/tmp/pxa-cpp-ref-final-sdk-20260930` 在仓库外再次构建 settings，
  输出 `/tmp/pxa-cpp-ref-final-external/pxa-settings.pxa`。
- `examples/conditional` 用 WASI SDK 34 生成 Linux x86_64、ESP32-S3、ESP32-S31
  的 Wasm/AOT。产品模拟器交互依次显示隐藏态、计数器 0、点击后 1、再次隐藏、
  再次显示 1；截图为 `/tmp/pxa-cpp-conditional-{initial,shown,incremented,hidden,restored}.png`。
  当前 SDK 包 `/tmp/pxa-cpp-sdk-conditional-20260930` 在仓库外 `/tmp` 工作目录
  用包内源码、CMake 和打包工具再次构建 Linux AOT，输出为
  `/tmp/pxa-cpp-conditional-external/pxa-conditional.pxa`。
- 容量推导与链式属性左右值类型测试通过，静态布局可以 constexpr 构造。
  当前原生计数器式页面对象为 312 字节，相同视图固定预留 32/32/4 槽为
  3224 字节；这是对象预留存储，不能代替 Guest/Host 的完整峰值内存测量。
- 导航测试覆盖固定容量、切换失败保留旧页面和任务、旧事件拒绝、页面任务
  取消与晚到结果、页面重建、稳态切换零堆分配。应用入口级测试确认可恢复
  UI 提交失败交给 `on_error`，应用继续接收事件，而非返回负值使 Host 停止。
- 前台任务测试连续完成并重用 16 次，验证已完成任务会回收有界协程槽。
- UI 位图测试使用 70 个绑定，覆盖跨 64 位边界、重复变化合并、只编码变化
  项、提交失败保留脏项、重试成功及页面析构后解除订阅。
- FS 测试覆盖非法路径/flags、路径存储与临时服务对象、移动与关闭、短读
  短写、定位、stat、目录项拥有存储、目录结束/畸形结果、最大 538 字节
  rename、取消以及晚到成功句柄回收。测试使用默认 1024 字节协程槽。
- Assets 测试先销毁路径字符串及临时服务入口，再启动 load/read/query，
  确认最终协议包路径和返回数据正确；资源取消及晚到回收仍通过。
  额外使用 Clang 的 AddressSanitizer/UndefinedBehaviorSanitizer、
  use-after-scope 和 detect_stack_use_after_return=1 运行，未报告生命周期错误。
  其余小型异步服务入口由协程按值持有，资源和大缓冲区按文档保持借用。
- `bash tools/package/test_guest_sdk.sh`：现有 C SDK 与应用回归通过。
- WASI SDK 34 x86_64 Linux 包已按锁定 SHA256 验证；旧工具链缓存保留。
- C++26 virtual-list 生成 Linux x86_64、ESP32-S3、ESP32-S31 Wasm/AOT；
  初次显示、滚动到 Item 5 并选择后显示 5 已在模拟器验证。
  LVGL 回归确认初始可见范围通知、删除时取消待发通知、绝对行位置参与父级滚动
  和失败回滚。Host 不再将 absolute 映射成不参与滚动的 floating。
- counter、storage、audio、navigation、files 生成 `linux-x86_64`、`esp32-s3`、`esp32-s31`
  对应 Wasm/AOT 包。game 已构建模拟器产物。
- 模拟器 counter 按钮从 0 增加到 1；settings 的 Toggle 与 Slider/Progress
  状态同步已做截图验证。
- 模拟器 game 创建固定尺寸上下文，显示绿色移动方块；连续截图内容不同。
  自动目标创建返回 unsupported：当前产品 Host 没有配置自动目标 profile，
  示例改为查询 Window 像素尺寸后创建固定尺寸上下文。
- 模拟器 storage 点击保存，停止后重启显示相同计数及 Loaded。
- 模拟器 navigation 的 Details 修改状态后返回 Home 保留计数；最新产物
  验证系统 Back 从 Details 返回 Home，再 Back 从 Home 正常退出（code=0）。
  桌面 Escape 与返回手势经 Window 服务交给应用处理，不直接强制退出。
  `PXA_SIM_NAV_MODE=buttons` 的底栏 Back 也验证了上述返回与退出行为。
- 模拟器 files 点击保存显示 1/Saved，停止重启后显示 1/Loaded。
- 模拟器 audio 获取 `audio.playback`/`media` 权限，open 与 graph 成功后
  tone 返回已接受。尚未通过录音验证实际声学输出。
- 更新后的 SDK 包 `/tmp/pxa-cpp-sdk-fs-20260929` 在 `/tmp` 工作目录用包内
  示例、实现源码、CMake 与打包工具成功构建 files Wasm/Linux AOT，并签名
  打包。编译器与发布者签名密钥由外部路径提供；不需要 PXA 源码参与编译。
  这验证文件示例的独立消费，完整示例集和多主机发布包仍需验收。
- C++26 开发包 `/tmp/pxa-cpp26-sdk-20260929` 的头文件、实现、CMake、
  特性探测与工具在包内完成 counter Wasm/Linux AOT 编译和签名打包；
  输出在 `/tmp/pxa-cpp26-external-output`，不使用 C Guest SDK。
- 更新的独立包 `/tmp/pxa-cpp26-sdk-modules-release-20260930` 在 `/tmp` 工作
  目录构建 modules 的 Linux/S3/S31 Wasm/AOT 并签名打包。应用、模型对象模块、
  SDK 实现与 CMake 均来自该包；只有锁定编译器和签名密钥由外部路径提供。
  这同时验证 CPP 对象模块的 include、C++26 和异常/RTTI 配置传播，以及固定
  文字描述在真实 WASI 编译中的使用。新异步入口的 audio 示例也完成 S31 AOT。
- Device 使用共享 schema 生成 C++ 有界拥有存储的运行信息与黄金向量，
  测试覆盖完整字段、截断、UTF-8、NUL、字段长度、临时服务入口、MAC 身份与
  flags、立即失败和 stop 阶段禁止导入；黄金数据来自现有协议文件。
- Sensor 测试覆盖完整目录后才发布结果、容量不足不修改输出、重复 ID/semantic、
  非法维数和语义、临时服务入口、调用时保存订阅参数、负值及 int32 边界、
  旧句柄/错误维数/不支持的样本数、移动关闭、取消晚到回收和 stop 后无导入。
  测试使用默认 1024 字节协程槽，全过程 new 计数为 0；这不代表 Host 零分配。
  Device/Sensor 另行使用 ASan/UBSan、use-after-scope 和
  detect_stack_use_after_return=1 运行通过。Sensor 增加 new 计数后也通过完整主机套件。
- 新独立包 `/tmp/pxa-cpp26-sdk-device-release-20260930` 含 schema、C++ 生成器
  与 Device 黄金向量；`--language cpp --check` 通过。在 `/tmp` 工作目录使用
  包内源码、CMake、打包工具构建 device-sensor 的 Linux/S3/S31 Wasm/AOT，
  分别输出到 `/tmp/pxa-cpp26-device-external-{linux,s3,s31}`。
- 产品模拟器增加 Sensor 服务：默认空目录，显式环境变量
  `PXA_SIM_SENSOR_TEMPERATURE_MILLI_CELSIUS=25000` 提供固定模拟源。
  已用独立包的 Linux AOT 验证 Device 信息、目录、精确 scope 权限、订阅、
  25000 毫摄氏度样本、Stop 和再次订阅；截图为
  `/tmp/pxa-cpp-device-sim-{sampling,stopped,resubscribed}.png`。
  模拟器退出 code=0。初次拒绝因模拟源语义长度少一字节，已改为 sizeof 推导，
  未放宽权限匹配或绕过授权。移除环境变量后再次运行，空目录显示 `No sensors`，
  截图为 `/tmp/pxa-cpp-device-sim-empty.png`，正常退出。
- `pxa_sensor_engine_test` 与 `pxa_lvgl_ui_test` 重新构建并通过，现有 C Guest
  的真实 WAMR Sensor 集成与 LVGL 回归保持可用。协议 schema 修正为当前
  Core v1 的 64 位句柄、原始订阅结果和单样本事件，没有修改 Host wire ABI。
- 新服务加入前后的 modules 应用，Wasm CODE 均 39733 字节、DATA 均 4442
  字节、初始内存均 2 页；移除调试节后均 54918 字节，Linux AOT 均 86276
  字节。符号表没有 Device/Sensor/FramePool 实现。原始 Wasm 增加 888 字节
  来自调试节，函数编号与重定位顺序有变化，文件不逐字节相同；此记录只证明
  本例未引入额外模块代码/数据体积，不能替代完整内存与性能报告。
- Net 主机测试覆盖直接编码到调用方包、临时 URL 生命周期、请求字段和所选头、
  404 作为有效响应、响应头拥有存储、流短读/EOF、无响应体的 204、容量不足、
  坏响应关闭流，以及前台取消后晚到流句柄回收。默认 1024 字节协程槽下通过，
  Net 定向测试也通过 ASan/UBSan 和 use-after-scope 检测。
  Core v1 的 64 位权限与流句柄同步修正至 Net 草案 JSON，Host ABI 未变。
  WASI SDK 34 在仓库内重新编译包含 `net.cpp` 的静态库和 counter Wasm；
  `/tmp/pxa-cpp26-sdk-net-release-20260930` 独立包在 `/tmp/pxa-cpp26-net-external-build`
  从包内源码、CMake 和示例构建 Wasm。尚未做 Net 实际 HTTP 后端及三目标 AOT 验证。
- IPC 原始接口测试覆盖 Core 接受后的 call ID、最终结果事件、提供方请求视图及
  `reply` 完成、临时 endpoint/payload 生命周期、容量错误、已知字段误标 optional、
  前台任务取消和晚到结果。独立的 IPC call ID 与普通 Core token 数值相同时，
  请求表按 service/opcode 分派，不会将 IPC 通知误投递给其他请求；类型错误的
  普通 Core 完成仍会交给原请求报协议错误。IPC 定向测试通过 ASan/UBSan 和
  use-after-scope 检测；WASI SDK 34 已编译 `ipc.cpp` 并链接 counter Wasm。
  更新后的独立包 `/tmp/pxa-cpp26-sdk-ipc-release-20260930` 在
  `/tmp/pxa-cpp26-ipc-external-build` 用包内源码与 CMake 构建 counter Wasm。
  类型化契约、独立多 Component 示例和真实 Broker 集成仍待验证。
- Work 定向测试覆盖 enqueue 的有界包与临时参数、取消、retry 完成、启动配置
  拥有存储、stop-requested 事件，以及 worker/延时/容量校验；ASan/UBSan 与
  use-after-scope 通过。`examples/work` 的 UI 与 job 两个 Component 使用
  WASI SDK 34 编译成 Wasm，并经 `package_app.sh` 生成 Linux AOT 与签名包
  `/tmp/pxa-cpp-work-pkg-v2/pxa-work.pxa`。`pxa_scheduler_engine_test` 通过。
  产品模拟器现已注册 Work capability 和 Scheduler，支持独立 job Component
  的激活、到期调度、完成、重试、取消和超时停止。用
  `build/simulator/pai-touch/pxsys_product_simulator --package
  /tmp/pxa-cpp-work-pkg-v2/pxa-work --publisher-key
  deps/pxa-system/apps/pxa/.dev-signing/publisher-public.der` 在 dummy SDL 下
  启动，控制套接字点击 Queue 后，UI 显示 Queued；状态存储生成
  `sync.completed.1` 值为 1，证明 job 实际执行。相同构建的
  `pxa_scheduler_engine_test` 和 `pxa_package_test` 均通过。模拟器 Work 队列
  只保存在进程内，重启后不恢复待执行项；ESP Host 尚未注册 Work，实机运行
  验收仍待完成。
- Surface 定向主机测试覆盖映射缓冲区对齐、移动 Surface 时帧租约保持有效、
  acquire 的 `would_block`、present、丢弃帧后关闭、状态查询、释放事件解码、
  预算不足及坏成功响应的句柄关闭；ASan/UBSan 通过。
  `examples/surface` 通过 WASI SDK 34 的 CMake 构建为 Wasm/Linux AOT，
  打包器验证内存最大值为 32 页，并把 pinned-memory 声明写入签名 manifest。
  产品模拟器 pai-touch profile 显示持续移动的 RGB565 条纹，两次截图
  `/tmp/pxa-cpp-surface-screen-{pinned,next}.png` 哈希不同。该证据仅证明
  模拟器的映射缓冲区、acquire/present 与显示路径，尚非设备或帧延迟验收。
- 最终 `examples/surface` 使用锁定的 WASI SDK 34 生成 Linux x86_64、ESP32-S3、
  ESP32-S31 的 Wasm/AOT 包；三次真实 CMake 构建均验证内存上限 32 页，
  生成签名 pinned-memory manifest。`test_package_tool.sh` 回归通过。
- 产品模拟器独立模式原先将初始状态设为前台却不发送首次前台事件，导致新版示例
  没有启动初始化。修复后最终 Linux AOT 在 pai-touch profile 显示条纹，截图
  `/tmp/pxa-cpp-surface-final-sim-fixed.png`；相关 ABI v1 pressure、IPC 与桌面
  smoke 三项 CTest 均通过。

## 实机

- S31 `/dev/ttyUSB0`、2000000：设备报告 `esp32s31`、固件 `520b46b`。
  storage 的 RISC-V AOT 包安装并启动；解锁后点击保存显示 1/Saved，停止
  后重启显示 1/Loaded。验证后已停止示例应用。
- S31 的 files RISC-V AOT 安装并启动；解锁后保存显示 1/Saved，停止后
  再运行并解锁显示 1/Loaded。截图为 `/tmp/pxa-cpp-files-s31-saved.png`
  与 `/tmp/pxa-cpp-files-s31-loaded.png`；验证后已停止应用。
- S31 实际安装运行 C++26 counter，初次截图显示 0；自动锁屏后恢复，
  继续点击显示 2，截图为 `/tmp/pxa-cpp26-counter-s31-final.png`。
  验证后已停止示例。这验证该次产物的 ABI/运行与 UI 恢复，不代表完整服务或
  当前所有模块的实机验收。
- S31 实际安装独立开发包生成的 device-sensor RISC-V AOT，解锁后显示
  `esp32-s31`、`wamr-pxa-aot-v6-core-1` 和 `No sensors`，截图为
  `/tmp/pxa-cpp-device-s31-info.png`，验证后已停止示例。该固件没有配置物理
  传感器，不能把空目录验收称为真实传感器采样验收。
- S31 安装并运行 C++ Surface RISC-V AOT。首次在 `on_start` 查询 Window 快照
  返回 `bad_state`：该设备在 Guest start 后才发布快照。示例改为首次
  `on_foreground` 初始化；解锁后日志确认创建 320×240、双缓冲 Guest-mapped
  Surface，注册 307200 字节像素并提交首帧。设备截图
  `/tmp/pxa-cpp-surface-s31-{first,second}.png` 显示 RGB565 条纹；画面区域
  两次截图有差异，Presenter 日志连续报告约 47 次/秒合成。截图元数据的
  `frame_id=0` 是 panel-framebuffer 截图路径，不能用作 Surface 帧计数。
  验证后已停止应用。尚未验证资源撤销、Guest/Host 峰值及长时间运行稳定性。
- S31 安装并运行更新后的 C++ GameRender 示例：在首次前台查询 Window 快照并
  创建 320×240 上下文，`/tmp/pxa-cpp-game-s31.png` 显示绿色方块。设备日志的
  一次采样为提交 160、渲染 120、可见 119、丢弃 40 帧；该数据提示默认
  16 ms 提交周期高于当时显示吞吐量，尚未完成游戏帧率与内存对照。
  验证后已停止应用。
- S31 的 `conditional` 包上传完成，安装两次均在 prepare-incoming 阶段因
  LittleFS `No more free space` 返回 -11。两次失败上传留下的该包临时文件均已
  清理，没有删除其他应用或数据；这次 UI 示例的 S31 实机交互尚未验收。
- pai-touch `/dev/ttyACM0`：设备报告 `esp32s3`、固件 `202d179-dirty`。
  storage 上传成功但安装失败，日志为 LittleFS `No more free space` 和
  prepare-incoming status=-11。已删除本次上传的 inbox 包；未删除已有应用
  或数据。该设备的 C++ 应用启动验收仍未完成。
- 本轮 pai-touch 的 device-sensor 上传完成，但安装仍返回
  `package_deploy_failed;stage=install;status=-11`。本次失败的日志导出只返回
  旧 storage 空间不足记录，尚无新的原因证明；已删除本次 inbox 文件，未动
  既有应用和数据。该设备的 Device/Sensor 执行验证仍未完成。

## 阶段体积

下表为 storage 相同源码的原始产物字节数。它不是与 C SDK 的性能对照。

| 产物 | 字节 |
| --- | ---: |
| Wasm | 223265 |
| Linux x86_64 AOT | 103720 |
| ESP32-S3 AOT | 173572 |
| ESP32-S31 AOT | 220104 |

files 的初次三目标构建原始大小：Wasm 225337、Linux AOT 110728、S3 AOT
183168、S31 AOT 230768 字节。其后 UI 位图与错误传播调整使独立包构建的
产物发生变化；这些数值用于记录该次构建，不作为最终 C/C++ 对照数据。

## 尚需完成

- 其他需要命令式访问的控件、显式常驻页面选项；完整容量配置及
  动态 UI 实机、更多事务失败路径验收。
- Work 的 ESP Host 接入、持久化队列与设备运行验收；Surface 在 pai-touch 的实机、释放竞态与资源撤销验收；Net 实际后端与设备验证、Device/Sensor 更多设备及撤销集成验证，IPC 契约生成器
  和独立 service/job Component 示例。
- Assets 的其余能力、图片/音效/音乐资源示例及真实资源撤销和取消竞态验证。
- GameRender triangle batch、资源批次、可选 3D 辅助模块、HUD、前后台及
  锁屏恢复；Surface 映射帧的真实资源撤销与内存峰值验证。
- 更新后的独立 SDK 开发包消费、版本发布说明、完整 CMake/WASI/Host 回归。
- 相同功能 C/C++ 的体积、分配、内存峰值、提交次数与延迟测量；不能以
  构建成功或当前截图替代性能证明。
- 两台设备的完整 SDK 验收，包括 pai-touch 空间限制的非破坏性处理。

## 语言模式与实现变化的阶段对照

同一计数器源码仅切换 C++23 到 C++26 时，Wasm 209883 字节、Linux AOT
87576 字节，两个文件均经 cmp 确认逐字节相同。显式对象参数与容量推导后、
文字存储调整前，为 Wasm 208842 字节、Linux AOT 84528 字节。
这些是 C++ 内部阶段对照，尚不是等效 C/C++ 的完整性能验收。

上述三次从配置到打包的单次 wall time 分别为 59.33、13.55、24.21 秒，
主机并发负载不同且包含工具运行，不能据此宣称编译速度改善。需要固定负载、
分开记录编译/链接/AOT、预热后重复采样，再报告语言模式与实现优化的收益。
