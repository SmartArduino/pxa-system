# 批量 PSG 合成（Audio 0.12 / 0.13）

普通提示音继续使用 `tone/effect`。需要精确维护相位、占空比、波表、LFSR
和阶梯包络时，使用 `psg_render`。Host 原生执行整数采样循环；输出混音和
DMA 仍由已有音频任务处理。此调用本身同步执行，不新增线程或后台队列。

```cpp
// 初始化一次。这里将 4194304 ticks/s 作为合成时钟。
auto pulse = pxa::AudioPsgVoice::pulse(1192, 0x0f); // 约 440 Hz、50% duty
pulse.envelope_period = 65536;                    // 64 Hz 包络步进
pulse.envelope_direction = -1;
pulse.left_gain_q8 = pulse.right_gain_q8 = 64;
std::array<std::byte, 40> state;
auto encoded = pulse.encode(state);
// 检查 encoded；保留 state，不能在下一批前重新用旧 pulse 覆盖它。

// 每批使用调用者持有的输出缓冲；Wasm 内部地址转换为显式 wasm32 偏移。
alignas(4) std::array<std::int32_t, 128 * 2> accumulation{};
auto result = session.psg_render(
    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(accumulation.data())),
    128,
    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(state.data())),
    1, 128); // 每采样前推进 128 ticks；输出为 32768 帧/s 的 S32LE 立体声
// 检查 result，然后滤波、限幅为协商的 S16LE 格式，调用 write_pcm。
// 复用 accumulation 前清零；state 保留 Host 更新的相位和包络。
```

`AudioPsgVoice::wave(period, table_offset, table_length)` 支持 1..32 个 S8
波表项。`noise(period, width, tap, inverted, seed)` 支持 7/15 位 LFSR、
可选 XOR/XNOR 和抽头。`AudioPsgVoice::decode(state)` 用于检查回写后的状态。
默认电平 15，Q8 增益 256 为单位增益；调用方可以左右声道分别调节。

每次只发送 20 字节命令；每声部状态 40 字节，直接留在 Guest 内存。
Host 在修改任何输出和状态前检查整批参数、重叠范围和工作量。不存在跨调用
保存的指针，因此应用暂停、关闭会话或释放内存后无需额外销毁合成器对象。
最大 8 声部、4096 输出帧、16384 声部帧、262144 次 LFSR 推进。

旧 Host 返回 `unsupported`，保留原软件合成路径。`psg_render` 的采样时钟
由调用方决定，不会自动匹配播放会话的采样率，也不负责插值和模拟滤波。
所有输入、回写状态和输出缓冲在调用返回前保持有效；返回后 Host 不再访问。

GB 的长度计数、扫频、DAC 偏置、波形 RAM 可见性和寄存器写入怪癖仍需由
APU 适配层处理。应在寄存器和帧序列器事件处切分批次，与原 APU 做音频及
CPU 可见状态对照，再启用替换。通用 PSG 并不等于完整的 GB 硬件模型。

完整字节布局与错误语义见 [Audio 协议](../../../spec/draft/audio.md)。

## GB 精确采样配置（Audio 0.13）

GB 模拟器可选择 `session.gb_psg_render(state_offset, output_offset, frames)`。
它使用独立的 128 字节 LE 状态和 16 字节命令，每批 1–128 帧，直接写入
调用方提供的 S16LE 立体声缓冲。`state_offset` 和 `output_offset` 都是
Guest 线性内存偏移；输出大小为 `frames * 4`，两段内存不能重叠。
该接口保留固定版本 mGBA 的方波、波形 RAM、7/15 位噪声累加、左右声道混合、
DAC 偏置和电容滤波算法。状态的完整布局见协议的 GB PSG profile 1 小节。

这里的时间戳使用双倍 GB 时钟（8388608 单位/秒），每个采样间隔 64 单位，
即 131072 Hz。先在传入时间戳采样，再推进时间；不能直接套用通用 PSG 的
先推进后采样约定。包络、扫频、长度计数与 CPU 可见的寄存器仍由模拟器
维护，控制事件发生前先完成此前的采样。Host 不持有跨调用的模拟器对象。

`gb_psg_render` 不另建任务，不分配 PCM 中间缓冲，也不直接播放声音。
调用返回后，沿用原来的重采样与 `write_pcm`。原有 Host 混音和 DMA 任务可
与 Guest 并行；本接口的合成循环同步执行。旧 Host 不支持或批次被拒绝时，
必须对同一时间段执行原软件采样循环，不能重复推进相位或丢弃采样。

## 通用 PSG 验证与资源开销（2026-10-21）

- 1800 组逐时钟参考对照：三种波形、XOR/XNOR、不同抽头、包络边界、
  多声部饱和累加，以及同一时间线分批后的样本和最终状态一致。
- 非法尾部声部、范围重叠、溢出与超额工作量均不修改状态或输出；合法的
  16384 声部帧和 262144 次噪声推进边界通过。
- Core 真会话、WAMR Guest → ABI → Host → PCM 通路、ASan/UBSan 通过。
- C SDK 全量使用 Clang 验证；GCC 音频单测通过；C++ SDK 全量通过，
  包括 C/C++ 逐字节编码一致性、短返回、旧 Host 拒绝和编码内存重叠。
- S31 RV32 ILP32F、GCC 16.1、`-O2` 交叉编译：内核目标文件 `.text`
  1604 字节，`.data/.bss` 均为 0；该函数静态栈 128 字节，不含调用链。

上述数据对应 0.12 通用 PSG 首轮验证；该轮尚未接入 GB，不能用它推断
S31 游戏 FPS、扬声器音质或整机内存峰值。

0.13 GB 采样配置已通过 24000 组完整通道状态/PCM 随机对照，以及 14 个实际
ROM 各 1800 模拟帧的 CPU、RAM、画面和 PCM 对照；另有 5 个实际 ROM 各
2400 帧通过 ASan/UBSan。Core 真会话和 WAMR Guest → Host 调用也通过。
S31 实际固件编译选项下，GB 采样内核 `.text` 2320 字节、只读表 132 字节、
`.data/.bss` 为 0，入口静态栈 288 字节，不含调用链。

GB 集成已在 GameKit-1 的 Audio 0.13 固件上测量。现有调用方每批通常只有
32 个样本、每模拟帧约 70–80 次同步调用，整条 Host 调用链比原软件循环慢，
因此该应用默认关闭 Host 后端。正确性通过不等于具有性能收益；增大批次
必须同时保留寄存器事件边界和 CPU 可见状态。当前没有把单独热循环的吞吐
当作游戏 FPS，也没有声称已完成扬声器听验或达到满速。
