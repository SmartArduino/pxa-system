# 批量 PSG 合成（Audio 0.14）

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
