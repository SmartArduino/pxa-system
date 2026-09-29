# C++ Guest SDK 验证记录

状态：开发中，尚未达到 `local/PLAN.md` 的完整交付条件。
以下为 2026-09-29 本地执行得到的阶段证据，不代表完整 SDK 验收。

## 已执行

- `bash tools/package/test_guest_cpp.sh`：17 组 C++26 主机测试通过，覆盖特性、Core、
  UI 编码/控件/状态/导航、生命周期、有界任务、Assets、Storage、FS、Permission、
  Audio、GameRender 创建/绘制/帧调度。
- 当前工具链 CMake 真实编译探测确认 C++26 模式、显式对象参数、包索引、
  expected/span/协程头文件可用；WASI libc++ 的 inplace_vector/function_ref
  编译探测失败，它们为可选能力，Guest 不依赖。主机为 Clang 22.1.8/
  libstdc++ 16，两项可选设施通过运行测试。C++23 编译公共头文件按预期报错。
- 动态列表测试覆盖稳定 key 重排复用、重复 key、结构提交失败保留回调、
  大范围跳转、旧事件拒绝、容量耗尽、行 State 的失败重试及离开行解除订阅；
  这些路径不触发 Guest 堆分配。局部字符数组副本和长只读文字描述挂载也零分配。
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

## 实机

- S31 `/dev/ttyUSB0`、2000000：设备报告 `esp32s31`、固件 `520b46b`。
  storage 的 RISC-V AOT 包安装并启动；解锁后点击保存显示 1/Saved，停止
  后重启显示 1/Loaded。验证后已停止示例应用。
- S31 的 files RISC-V AOT 安装并启动；解锁后保存显示 1/Saved，停止后
  再运行并解锁显示 1/Loaded。截图为 `/tmp/pxa-cpp-files-s31-saved.png`
  与 `/tmp/pxa-cpp-files-s31-loaded.png`；验证后已停止应用。
- pai-touch `/dev/ttyACM0`：设备报告 `esp32s3`、固件 `202d179-dirty`。
  storage 上传成功但安装失败，日志为 LittleFS `No more free space` 和
  prepare-incoming status=-11。已删除本次上传的 inbox 包；未删除已有应用
  或数据。该设备的 C++ 应用启动验收仍未完成。

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

- UI 条件分支、Ref、嵌套动态模块、显式常驻页面选项；完整容量配置及
  动态 UI 实机、更多事务失败路径验收。
- Net、IPC、Work、Sensor、Device、Surface 服务封装；IPC 契约生成器
  和独立 service/job Component 示例。
- Assets 的其余能力、图片/音效/音乐资源示例及真实资源撤销和取消竞态验证。
- GameRender triangle batch、资源批次、可选 3D 辅助模块、HUD、前后台及
  锁屏恢复；Surface 映射帧所有权验证。
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
