# 图像解码：资源源、平台后端与内存

应用把 HTTP 响应体或 `File` 移交给 `assets().decode_image`，得到可供 UI/Canvas 使用的独占
`Asset`。它与包内 `load(AssetKind::image, path)` 的结果使用同一套引用、绑定和关闭机制。
图片与解码任务的源对象相互独立。示例见 [C++ SDK](../../sdk/guest-cpp/README.zh-CN.md)。

## ABI 的边界

Assets 的可选 `image-decode` 能力使用 `DECODE_IMAGE=6`：

```
source:u64 | resource_type:u16 | options:u16 | max_width:u16 |
max_height:u16 | max_pixels:u32 | max_encoded_bytes:u32
```

结果是 `image:u64 | info:20 bytes | source_width:u16 | source_height:u16 | encoded_bytes:u32`。
状态前缀和请求取消沿用 Core。没有新 import、独立解码器句柄、写入操作或 finish 命令。
本轮尚未发布的两阶段 image-open/image-finish 草案已经替换，不保留兼容。

源为同一组件的 FILE(3)/STREAM(2)，从当前读取位置开始。Host 借用源，调用者在任务结束前保持源
独占可读。C++ SDK 按值取得移入对象并自动关闭它；原始 ABI 不替调用者关闭源。每次读取都通过
Core 校验完整句柄的代数、类型和所属组件，权限撤销会拆除对应句柄。Core 请求身份防止取消后
复用 token 的任务收到旧结果。结果发布失败时关闭新图片；停止组件时回收所有待处理任务。

每次 poll 默认最多读取四个 1024 B 块，栈上复用，不按图片大小分配 Guest 或 Host 输入缓存。
签名探测先读最多 64 B；codec 可在 open 中调用 `pxa_image_target_set_input_budget(target, bytes)`
收紧后续 poll 的编码输入预算，零恢复默认值。流式 PNG 使用 64 B，让 owner loop 在解压片段之间
处理输入、取消和显示；codec 描述符、wire ABI 与 SDK 均不变，也不增加解码器实例大小。
这是编码工作量上限，并非 CPU 毫秒期限：高压缩比输入、finish 与多帧 next 仍可能耗时。
WOULD_BLOCK 立即返回，网络工作线程可继续下载；无需新的图片专用线程。JPEG 的 finish 仍是一次
同步 native 解码，不能把整个过程描述为硬实时或无延迟。大图进一步拆分 JPEG CPU 工作可在后端
内部演进，不必改变应用和 wire。Host 内仍有网络缓冲、codec 输入缓存和像素写入，不能称为全链路
零拷贝；此处消除的是编码数据的 Host → Guest → Host 往返和逐块 Wasm 调用。

Host 在 Guest 回调/取消后和源 I/O 通知后调用 `pxa_assets_service_poll`，返回非零则安排下一次
owner-loop 调度（继续分片或递送完成事件）。WOULD_BLOCK 不请求重调度，等待源的原生通知；
HTTP 即使没有待完成的 Net 请求也必须转发数据就绪通知。这样普通文件和无时钟订阅的应用也能
完成解码，不依赖游戏 tick、不忙轮询，不新增常驻线程、状态表或唤醒回调存储。

## 新平台接入

`libpxa` 只负责请求、源资源、缩放目标、像素格式转换、限额和不可变资源，不链接任何图像 codec。
可选 adapter 在产品中链接。已有 LVGL 的产品通常只需：

```c
#include "pxa/image/codecs.h"
config.image_backend = pxa_lvgl_image_backend();
config.image_allocator = pixels;             /* 生命周期长于最后一个图像引用 */
config.image_temporary_allocator = temporary;
```

复用产品已注册的 LVGL 解码器，无需每个板子复制 PNG/JPEG 实现。与现有 LVGL UI 适配器一样，
支持的 LVGL 版本由产品固定；会话结构的私有头兼容集中于这一文件，应用和通用层不接触它。
adapter 使用 no_cache 并移除临时源的 header cache，不增加图片常驻缓存。支持完整像素缓冲与
get_area 分块输出，接受 RGB565/BGR888/BGRA8888。其他格式明确返回 unsupported。
预乘 alpha 的 native 缓冲同样明确返回 unsupported，避免把透明边缘错误变暗。
共享适配器使用 LVGL 的变量图片源（RAW 内存 descriptor），产品所选 decoder 必须支持这种源；
仅支持文件路径的 decoder 需在平台适配器中接入其内存源，不能假定启用 MEMFS 就自动支持。
硬件 decoder 的注册、DMA 和工作区按产品配置集中处理。

ESP 产品可选 `pxa_esp_rom_image_backend()`，桌面可选 `pxa_posix_image_backend()`。
自定义平台/硬件只需一个 `pxa_image_codec_t`，实现四个会话回调：

| 回调 | 职责 |
| --- | --- |
| open | 探测八字节签名；不支持则返回 unsupported，成功交出会话 |
| write | 同步借用输入块；本次全部消耗或失败 |
| finish | 检查完整性并完成像素输出 |
| close | 在成功、失败、取消路径上销毁会话 |

静态 codec 列表按顺序探测；Core 自动关闭会话。后端可使用官方解码 API、设备 ROM、硬件驱动，
不需要理解 HTTP、FS、Wasm、组件身份、取消事件、Canvas 或资源句柄。所有平台共用
`pxa_image_target_begin/blit`：先检查原图尺寸，再选有界目标，native 缩放/MCU/扫描线转换成 tile。
新增 codec 不需要扩充 ABI 或 SDK。输入签名匹配并不代表该 codec 支持所有变体；真正不支持的
JPEG 子格式可以在 finish 返回 unsupported，列表不会在已经输出部分图像后盲目重试其他后端。

## 默认选择及取舍

| 平台/格式 | 实现 | 原因 |
| --- | --- | --- |
| PNG | 可选 Pngle/miniz 流式 adapter | 缩小后输出、透明度、Adam7；避免完整原图像素缓存 |
| ESP32-S3 JPEG | 官方 ROM TJpgDec | 复用设备已有代码，有界工作区与 MCU 输出、1/2/4/8 IDCT 缩小 |
| 桌面 JPEG | 系统 libjpeg/libjpeg-turbo | 平台维护的 decoder、行输出和 IDCT 缩小，不维护 PXA JPEG 源码副本 |
| 其他 LVGL 平台 | 已注册的 decoder | 共享适配器复用平台原生实现；加速与工作区取决于该实现 |

LVGL 是解码注册与调用框架，不等于硬件。ESP32-S3 的 ROM decoder 仍在 CPU 上运行；
ESP32-S31 与 ESP32-P4 的 JPEG 硬件是另一种能力。当前默认 ESP 配置选择有界 ROM 路径，
没有声称已在 S31/P4 验证硬件收益。硬件接入也必须满足目标像素布局、DMA 对齐和峰值预算；
不能为“硬件加速”强行展开大原图。具有硬件 decoder 的产品可以优先注册对应 codec，应用无需迁移。

当前 LVGL LodePNG 会解码完整原图，简单替换会增加大图峰值。流式 PNG 因此是可选的内存优化
后端，并非 Guest 自写解码算法。上游代码、许可证和集中维护的加固差异在 adapter/vendor 中。
本轮移除了 PXA 的 TJpgDec 源码副本。压力验证还发现当前 LVGL TJpgDec 在畸形数据上的有符号
整数溢出，桌面默认避免使用该实现；没有修改托管 LVGL 源码或隐藏 sanitizer 报错。
JPEG 原生解码前共用一个小型结构检查器，仅检查元数据、尺寸与表合法性，不重复实现熵解码。

## 内存与限制

### `esp_new_jpeg` 的 ESP32-S3 对照

2026-10-15 在 pai-touch、240 MHz、输入与 RGB565 输出均位于 PSRAM 的独立诊断中，
同一张 320×180 基线 JPEG 的 ROM 路径平均为 51.49 ms，`esp_new_jpeg 1.0.2` 为 12.03 ms。
这证明它值得考虑，但不是浏览器或显示 FPS。其原生工作区实测占用 9980 B SRAM，
缩放案例为 27032 B SRAM；ROM 使用 8224 B PSRAM（含堆开销）。
分块不能与缩放组合，320×180 输入也未通过分块解码。

默认继续使用 ROM，保持现有 SRAM 与峰值预算。该库目前没有公开的逐实例工作区分配器；
直接接入会使原生分配绕过应用预算。也不为此复用截图服务的私有分配符号包装。
未来能满足预算的实现仍只需增加平台 codec，ABI 和应用 SDK 无需变化。
可复现诊断位于工作区 `firmware/components/pxa/tests/jpeg_benchmark`；
库的格式、对齐和模式限制参见 [Espressif 的实现说明](https://github.com/espressif/esp-adf-libs/blob/master/esp_new_jpeg/README.md)。

RGB565 tile 写入 RGB565 目标时直接保留原生样本，避免三通道展开后再量化。
同机公共像素处理基准由 26.35 ms 降至 18.99 ms，输出校验及预算峰值一致。
ROM 的 RGB888 和 PNG 路径保留原算法，不据此宣称它们获得相同加速。

### 资源边界

未使用图片功能时不创建图片任务、缓冲、线程或全局缓存。每个在途任务动态分配小型状态，
静态解码成功后只保留最终像素。输入/输出上限属于调用者的安全预算，后端不能通过先展开大图再缩小绕过。
当前输出边长最多 1024、像素数最多 65536；编码输入最多 512 KiB。PNG 原图边长最多 4096、
总像素最多 4194304。源面积限额约束解压工作，PNG 行缓冲按宽度分配，未增加完整原图缓存。
JPEG 的源限制由平台后端检查；不能用 PNG 的限制推断任意平台 JPEG 支持范围。
比例保留、就近采样，不上采样；较小目标会损失细节。默认后端支持普通 RGB
JPEG 和 PNG；桌面 libjpeg 还支持灰度 JPEG。ESP32-S3 的旧版 ROM 不支持灰度 JPEG，
真机验证会返回 unsupported 并保留替代文字；不能把桌面结果当作设备支持。
不支持 progressive JPEG、WebP、SVG。启用 GIF 的 LVGL 产品可选用下述多帧路径。

pai-touch 的源、输出和解码临时空间使用已有 PXA 预算与 PSRAM 分配器。JPEG 仅保留有界压缩
数据与工作区；PNG 仅保留 inflater、扫描线和最终像素。临时 resize 同时计费旧块和新块。
第三方 LVGL/libjpeg 的内部工作区归其 native allocator 管理，不冒充已经计入 PXA 预算：新平台
必须用堆监测核对真实峰值；LVGL 全图 decoder 尤其要考虑原图展开。需要严格预算的设备应选择
ROM/流式后端或提供支持预算分配的 native adapter。禁止用全局 malloc hook 偷改其他任务的分配。

验证必须同时检查像素、透明度、截断/畸形数据、分配失败、取消/权限撤销、重复启动及真实堆峰值。
浏览器的可重现设备对比与截图见 pxa-apps/browser/docs/images-20261015.zh-CN.md。

## 按需多帧图片（Assets 2.3）

可选 `image-frames` 能力为 DECODE_IMAGE 增加 options 和多帧结果，并提供
`NEXT_IMAGE_FRAME=7`。没有新增 Core 资源类型或默认句柄表容量。
options 的 PNG=1、JPEG=2、GIF=4 位限制允许格式，零表示任意受支持格式；在原生 codec
分配前按签名检查，不能仅凭网址后缀或服务器 MIME 绕过设置。

`WITH_FRAMES=256` 将请求扩展至 32 字节，末尾为两个 u32：动画像素、编码字节上限。
零沿用普通上限；非零只收紧序列 codec，不降低 PNG/JPEG 的分辨率。
结果扩展至 48 字节，增加 `delay_ms:u32 | iteration:u32 | flags:u32`，LAST=1。
静态图片只返回一个 LAST 帧，随后没有解码状态。

NEXT 借用当前图片的 u64 句柄，返回相同结构，转移一个新的 IMAGE 句柄。
解码状态随新图片转移，旧图片的不可变像素仍可被 UI 引用。普通图片资源槽位中的 ticket
与动画状态使用 union，不增加每个资源槽位的存储。请求仍使用既有排队、配额和取消机制；
切帧期间需要一个额外资源槽位，关闭最后的状态所有者或停止组件均释放解码器。
取消发生在 Host 推进之后时，迟到结果拥有状态，SDK 会关闭它；调用者应丢弃旧序列并重载，
不能假设被取消的请求完全没有执行。错误帧也不会改写旧图片。

C++ 应用按需包含 `<pxa/image_frames.hpp>`，使用 `assets().images().decode(...)` 和
`DecodedImage::advance()`。没有内置计时器或帧队列，由应用按可见性与显示节奏推进；
切到后台时取消在途任务并释放动画，避免持续解码。参见 SDK 的生命周期说明。

GIF 适配器使用 LVGL 的原生扫描线 LZW 内核，有界保存编码输入，仅合成缩小后的画布；
处置 3 可增加一份缩小背景。包含透明像素时为 BGRA8888，全不透明时采用 RGB565。
下载完成后才显示首帧；不支持 GIF Plain Text、交互等待及少见的超长未清字典流。
产品需启用 LV_USE_GIF，源码桥集中于适配层；仅提供预编译 LVGL 而无对应源码时为
unsupported。源码与新平台说明见 `libpxa/adapters/image/README.md`。
