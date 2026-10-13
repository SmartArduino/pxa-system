# C++ 音效接口迭代与验证（2026-10-12）

基于 `2729958`，本轮完善 C++ API、生成检查与开发体验。Audio 0.9
线上格式和 Host 混音器保持不变；没有添加默认缓存、队列、协程或堆分配。
工作区其他输入法和应用重命名改动保留。

## 修改与取舍

| 修改 | 原因与收益 | 内存/性能影响 | 验证 |
|---|---|---|---|
| `effect(layers)` 独立播放；`effect(layers, EffectTrack)` 原子替换 | 调用者不用记忆 `255`；类别可用 `constexpr auto weapon = EffectTrack::track0` 命名 | 仍然一次 I/O，O(层数) 的验证和编码 | 签名 C++ AOT、SDK 字节黄金样本、全部六个类别 |
| `SoundTrack` 与 `EffectTrack` 分离 | 防止把资源音效与合成音效的类别混用 | 两种枚举均 1 B，API 不保存轨道对象 | 错误类型及裸整数无法编译；显式无效 cast 在 I/O 前拒绝 |
| 操作码与布局/数量上限从协议 JSON 生成 | 避免 SDK 多处手写操作码和限制漂移 | 编译期常量，无新增默认存储 | 生成检查、Host 常量对齐断言、完整协议校验 |
| `[[nodiscard]]` 接收结果 | 忽略拒收会无声丢失音效；编译器给出原因 | 仅编译期诊断 | 忽略独立/命名播放和停止结果时编译失败 |
| 显式写入传输字节 | 小组不再清零全部 116 B 容量；包括每层保留位在内的传输字节均确定 | 缓冲上限仍 116 B；不保留输入 span | ASan/UBSan、自动变量填充模式、黄金字节、1..8 层 |
| 关闭/移走会话的同步操作返回 `bad_state` | 不向 Host 提交零句柄 | 增加轻量资源检查，无附加状态或分配 | 移动、关闭、PCM、音效与停止均无无效 I/O |
| 可操作的 SDK 示例与 SDL 回归 | 展示独立、替换、停止及忙碌处理；只有 graph 成功后才发布可用会话 | 示例 UI 自身增加按钮，不代表 SDK 默认开销 | 296×240/160 DPI 与 480×480/320 DPI 圆角配置截图；重复激活 |
| Jump Jump 轨道调用迁移 | 适配强类型资源音效接口，保持原来的六个音效分类与独立 BGM | 无新增游戏状态 | C++26 O3 编译、原生应用回归 |
| 渲染所有权检查识别 C++ `game::Renderer` | 原检查仅识别旧 C 名称，误报已迁移游戏 | 测试工具变化 | 实际应用目录检查通过；缺少渲染器的负例仍被拒绝 |

没有添加预编码音效对象或复杂调度器。此处每组最多 8 层，尚未证明重新编码
是游戏瓶颈；缓存会引入持久占用与更多生命周期约束，需先测量再决定。

## 开发与兼容说明

这是源码 API 更新：旧的 `effect(tones, 3)` 改成
`effect(tones, EffectTrack::track3)`；`stop_effect` 同理。
`sound_track` / `control_sound` 的整数类别改成 `SoundTrack`。
二进制 ABI、操作码、载荷、采样时钟和音效配方没有改变。

`AudioSession::max_effect_layers` 是组的层数上限；
`effect_track_count` / `sound_track_count` 是分类数量，各自当前为 6。
它们不保证并发：ESP/SDL 合成会话目前共用 4 个 PCM/音效槽位。
已存在类别可在满容量时原子替换；新的类别或独立播放可能返回 `would_block`。
此时整组未被接受，没有后台排队；游戏决定丢弃或重试，不能忙等。
停止/替换不能撤回已经提交给音频设备的采样。

## 实测与限制

同一 x86_64、Clang 23.1.1、C++26、O3 编译器探针对比旧/新头文件：

| 指标 | 修改前 | 修改后 |
|---|---:|---:|
| `AudioSession` 原生对象 | 40 B | 40 B |
| `Tone` 原生对象 | 14 B | 14 B |
| `Result<void>` 原生对象 | 8 B | 8 B |
| 编码函数静态栈帧 | 152 B | 152 B |
| 包装调用栈帧 | 8 B | 8 B |
| 编码探针 `.data` / `.bss` | 0 B | 0 B |
| 编码探针 `.text` | 1817 B | 1899 B |

152 B 是编译器栈帧，包含寄存器保存/对齐等，不能与源代码中的 116 B 数组混为
一谈，也不是整个调用链峰值。探针无分配器引用；它调用的 `Transport::io`
只检查参数并转入 `pxa_io`。原生代码增加 82 B，不能宣称代码体积没有增加。
它不是设备 AOT 映射大小，也不能据此推断 SRAM/PSRAM 的实际峰值。
Host 的帧/深度缓冲、音频存储和队列本轮均未改变。
完整编译器数据见 [对比 JSON](audio-effects-20261012/codec-comparison.json)。

本轮没有重新测量真机整体 SRAM、PSRAM、AOT 映射、游戏 FPS 或扬声器延迟。
不能把原生编译结果当作这些设备指标，也不宣称本轮提高了 FPS。
新的 SDK 示例完成 ESP32-S3 O3 AOT 构建；真机排队改善的前一轮证据在应用仓库
`maze-evil/docs/audio-optimization-20261012.zh-CN.md` 中。

原生覆盖音频、Assets、服务生命周期、权限所有权、features；音频扩展覆盖
黄金字节、保留位、最大层数、全部轨道、无效参数零 I/O、拒收无重试、短/超长
Host 返回、资源移动/关闭和迟到资源响应回收。ASan/UBSan 与自动变量填充模式
通过，确保未初始化的备用容量不被传输。

真实签名 C++ AOT 在 Core/WAMR/LVGL/SDL 路径中验证了两层共同时间线、独立播放、
同轨替换、停止幂等性、旧 tone FIFO、真实焦点暂停/恢复、四槽容量拒收不变更状态、
满容量同轨替换，以及两个屏幕配置上的重复激活和清理。SDL 后端原有 PCM/
Vorbis/Opus、事件、替换与背压检查通过。
截图：[296×240](audio-effects-20261012/profile-0.png)、
[480×480 / 320 DPI](audio-effects-20261012/profile-1.png)。

## 复现

在 pxa-system 根目录执行（签名密钥默认使用本地开发密钥）：

```sh
python3 spec/draft/tools/generate_service_codecs.py --check --language cpp
bash tools/package/test_guest_cpp.sh
python3 tools/package/measure_cpp_audio.py --baseline 2729958 --output /tmp/pxa-audio-codec.json
cmake --build /path/to/simulator-build --target pxsys_audio_sdk_test pxsys_audio_test -j 4
PXA_SDK_TEST_ARTIFACT_DIR=/tmp/pxa-audio-captures bash tools/package/test_cpp_audio.sh /path/to/simulator-build/pxsys_audio_sdk_test
```

新增 CMake 目标需要先重新配置模拟器；也可以通过 CTest 的
`pxsys_audio_sdk_test` 执行完整签名构建与回归。`test_guest_cpp.sh` 是完整 SDK
检查入口，本轮运行的是相关五项原生检查和编译诊断，未重复整个 SDK 全量套件。
协议校验使用隔离副本排除了工作区尚未完成的 IME 清单变更，原工作区未被修改。
