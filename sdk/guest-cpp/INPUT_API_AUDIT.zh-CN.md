# 输入框与应用层 wire 使用审计（2026-10-10）

本轮检查 `local/pxa-apps` 的 C++ 业务源码及 SDK examples，承接已有
`API_AUDIT.zh-CN.md`，不重复已完成的 Storage、Binary、FixedText、RGB565 等封装。

| 原因 / 应用原调用 | C++ SDK 接口与迁移 | 收益及内存变化 | 验证 |
| --- | --- | --- | --- |
| 阅读器 `write_input_box` 自行构造颜色、布局和输入事件绑定 | `TextInput(...).box(State<TextInputBox>&)`，保留 `max_bytes`、`single_line`、`TextInputRef` 和提交回调 | 删除应用自定义输入控件；普通输入框仍为一个指针、1 绑定、1 文本回调；可选 box 增加 1 绑定，没有缓存或堆分配 | 命令属性、中文/emoji、提交、键盘开关、状态去重、卸载、错误 PATCH 回滚、ASan/UBSan |
| 阅读器从 START / UI 环境 TLV 手读功能位 | `decode_start_ui_capabilities`、`decode_ui_capabilities`、`UiFeature`、`supports` | 能力结果独立于 DisplayMetrics，不扩大游戏默认几何状态；阅读器缓存仅保留实际需要的布尔值 | 完整环境、未知能力位、重复/截断、错误 service/opcode/token |
| Pixel Dungeon 从 START 手读系统语言 | 可选 `startup.hpp` 的 `decode_start_system_environment`、`SystemEnvironment::is_language` | 无复制、无缓存；只借用配置；支持可选文本方向 | zh-Hans-CN、en-US、ar、ASCII 子标签、方向、重复和截断 |
| Jump / Pixel Dungeon 手读 CLOCK_TICK 的 64 位时间戳 | `decode_clock_tick` / `ClockTick`；可选 EventTraits | 不增加 Event 字段或运行时路由表；损坏通知不会推进模拟 | service/opcode/token、精确 8 字节、零时间、额外尾部及 Event 布局 |
| 游戏输入区与 UI 自行组装透明颜色 | `Transaction::rgba`，Page::rgba 复用 | 固定 8 字节编码；不增加 Transaction 存储 | SDK 页面回归及应用 AOT 构建 |

阅读器布局 PATCH 命令从 149 字节降为 124 字节（少写长度限制 14 字节和单行模式
11 字节）；调用与事务次数没有改变。测试确认短字符串编辑、box 更新和键盘控制
不新增动态分配。动态长文本仍按实际内容分配，不能将其误报为所有输入均无分配。

原生 x86_64、Clang C++26 的实际 `sizeof` 对比：ReaderApp **33472 → 33464 B**，
view **88 → 88 B**，Page **432 → 432 B**。TextInputBox **24 B**，与原应用 InputBox
相同。此结果是静态对象布局，不能代替设备 SRAM/PSRAM 或运行时堆峰值测量。
没有新增帧缓冲、字体图集、默认服务状态或事件缓存。

本轮结束后，上述应用业务源码没有直接 `pxa::wire` 使用。以下保留底层编码是合理边界：

- SDK 内部实现协议。
- 自动生成的 `examples/ipc-stats/stats_contract.hpp`；应用调用类型化 IPC 接口，生成器负责协议。
- 协议测试及 Pixel Dungeon `tools/render_test.cpp`，需要独立检查字节与错误消息。
- 高级自定义 UI 视图可以使用 Transaction 的类型化属性，普通输入框无需自己创建节点或绑定事件。

回归：`bash tools/package/test_guest_cpp.sh`；新增 `startup_test.cpp`、
`text_input_box_test.cpp` 并扩展 `events_test.cpp` / `ui_display_test.cpp`。
同时修正既有 `ui_controls_test.cpp` 的事件夹具：原来仅有 88 字节，却在动态文本
用例中写入 2000 字节；现在按协议上限使用有界数组，ASan/UBSan 长输入检查通过。
阅读器、Jump、Pixel Dungeon 的模拟器 AOT 构建通过；阅读器另验证 Xtensa S3 与
RISC-V S31 AOT。应用端截图及可复现步骤见 pxa-apps 的
`novel-reader/docs/2026-10-10-sdk-mobile-ui.zh-CN.md`。
