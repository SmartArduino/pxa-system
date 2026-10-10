# 动态系统文本输入验收（2026-10-10）

UI 0.8 的 `dynamic-text` 增加可选输入容量和单行模式。

```cpp
State<std::string> query{""};
TextInput(query, editor).max_bytes(1024).single_line().on_submit([this] { search(); });
```

容量是 UTF-8 字节数；`max_bytes()` 使用协议上限 4052，允许 1..4052。
Core 1 消息最大 4096 字节，扣除 20 字节信封和 24 字节 UI 前缀。
文本事件按内容长度发送，空字符串也是有效编辑；超限在完整 UTF-8 边界截断。
组件清单声明 `dynamic-text`。未配置容量的旧输入框仍使用 64 字节。

长文本由 LVGL、Core、ESP 命令队列和 C++ Page 全链路支持。
ESP 的 64 字节短文本仍内联；长文本只分配实际字节数，优先 PSRAM。
入队失败、过期实例和正常交付均释放所有权，没有新增默认大缓存。
命令槽仍为 144 字节；LVGL 节点和事务回滚快照沿用现有字段。
普通 `TextInput(value)` 仍只有一个指针大小。

长网址应使用 `single_line()` 横向滚动。真机测试发现多行折行长单词
会耗费大量排版时间并触发看门狗；单行模式消除了该测试中的问题。
`single_line(false)` 保留多行行为；改变模式保留应用设定的高度。
Host 还跳过状态绑定写回的相同文本，避免重复分配、排版和重置光标。
这不保证任意几千字节的多行编辑在所有字体/设备上具有相同响应速度。

## 自动检查

从 PXA 工作区根目录运行：

```sh
bash deps/pxa-system/tools/package/test_guest_cpp.sh
bash firmware/components/pxa/tests/test_host.sh
ctest --test-dir build/resource-checks/lvgl --output-on-failure
ctest --test-dir build/simulator/pai-touch --output-on-failure
```

本轮 C++ 套件、ESP Host 套件、37 项原生检查、15 项模拟器检查通过。
Core 覆盖空事件、65 字节、完整 4052 字节事件、超限和损坏 UTF-8。
LVGL 覆盖截断边界、清空、撤销容量/单行属性、失败事务回滚、固定高度
及重复写回时保留光标。Host 覆盖按实际字节分配、OOM、队列失败和过期释放。
保留 C SDK 的旧默认缓冲，同时更新长事件解析；兼容回归也通过。

## 真机复现

验收应用只使用独立 ID `pxa-cpp-input-qa`。运行前解锁屏幕，保持中文 K9
系统键盘布局。测试有等待时间以确保异步键盘显示；脚本耗时不是输入延迟测量。

```sh
bash tools/app.sh build dynamic-input   --source-root deps/pxa-system/sdk/guest-cpp/examples/system-input/tests   --target esp32s3 --aot-only --output local/dynamic-input-qa
python3 tools/pxadb/pxadb.py package install   local/dynamic-input-qa/pxa-dynamic-input.pxa --port /dev/ttyACM0 --yes
python3 deps/pxa-system/sdk/guest-cpp/examples/system-input/tests/verify_dynamic_input.py   --port /dev/ttyACM0 --output local/dynamic-input-qa/device
python3 tools/pxadb/pxadb.py package uninstall pxa-cpp-input-qa --port /dev/ttyACM0
```

pai-touch 的 296×240 真机与完整系统模拟器均用系统键盘删除一个字符后提交。
验收比较完整字符串；哈希供核对日志。包括中文 UTF-8 和长网址，非预置值假验收。

| 初始字节 | 编辑后字节 | FNV-1a（验收自定义初值） | 内容比较 |
|---:|---:|---|---|
| 500（160 个汉字和 20 个 ASCII 字符） | 499 | `456cba11b3574340` | PASS |
| 2048（网址） | 2047 | `b6395141d531375c` | PASS |
| 4052 | 4051 | `2275c5468bb636f9` | PASS |
| 1 | 0 | `14650fb0739d0383` | PASS |

三次重复启动/停止也通过。只更新 pai-touch 的应用固件分区，已备份该分区；
未写入引导程序、分区表或应用数据。esp-mosaico 固件构建通过，尚未部署本轮更新。

内存结果见本次工作区 `local/dynamic-input-20261010/device-final/memory.json`。
`initialization` 和 `edits` 分别用 `MEMORY START/STOP` 记录本次窗口真实最低空闲堆；
`idle/after_stop` 的 `*_min` 以及资源预算 `peak_*` 是历史值，不能当成本次峰值。
长输入所需字符串、LVGL 文本、Core 事件和队列所有权会占用实际内容的空间，
因此本改动不承诺“任意长度输入都与短输入占用相同内存”。新增默认容量缓存为零。

最终 pai-touch 数据（字节，含系统界面及其缓存）：

| 阶段 | SRAM 稳定使用 | SRAM 本窗口真实峰值 | PSRAM 稳定使用 | PSRAM 本窗口真实峰值 |
|---|---:|---:|---:|---:|
| 初次应用初始化 | 264960 | 269184 | 1808016 | 2318308 |
| 键盘编辑窗口结束 | 265144 | 269332 | 1826248 | 2377456 |
| 三次重复启动后停止 | 263952 | — | 1402312 | — |

Guest 线性内存为 131072 字节，启动日志的 WAMR 动态占用 31137 字节、峰值 31141；
Core 资源预算固定 2224 字节，内部 27468 字节，外部编辑时 16630 字节、停止后 16516。
本次窗口未增加资源拒绝和分配失败（均为零）。事件队列扩容为零；原有最大消息
工作区不变。GUI 和输入没有增加 Guest 帧缓冲或深度缓冲。

相对冷启动前，停止后系统保留 272 字节 SRAM 和 42528 字节 PSRAM。
这些数据包含首次键盘、字体和截图初始化缓存；本次没有用旧版执行相同长输入
的对照（旧版无法接收该工作负载），不能将其直接归因于动态文本或声称全堆零增长。
Host 所有权回归单独证明每条长事件准确释放；默认静态内存不增加。
