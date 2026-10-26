# PXA 系统许可证准入规则

此规则适用于固件、Host 服务、渲染器、音频实现、系统 UI、运行时和随系统
分发的依赖。应用包中的游戏引擎、模拟器及素材独立审查，不纳入系统代码。
不得把应用中的受限实现复制、改名或改写后放入系统。

系统禁止采用 GPL、AGPL、LGPL、MPL、EPL、CDDL、EUPL、OSL、SSPL 等
copyleft 实现。默认选择 MIT、BSD、Apache-2.0、ISC、Zlib 等经核实的许可。
双重许可必须明确选择允许的分支并保留相关声明；`AND` 的每个条件都必须
满足，不能把包含一个 MIT 分支误当成整个组合均为 MIT。

只允许逐项核实的运行时例外。当前 GCC libgcc/libstdc++ 使用
GPL-3.0 WITH GCC-exception-3.1，限工具链运行库及其必要头文件，使用符合
例外要求的正常编译过程。该例外不能用于任意 GPL 库。WAMR 的
Apache-2.0 WITH LLVM-exception 也保留原有声明。

依赖选择：

| 依赖 | 系统采用的许可 | 要求 |
| --- | --- | --- |
| PXA 自有实现、LVGL 主体、FreeRTOS、cJSON、pngle | MIT | 保留版权与许可 |
| WAMR、ESP-IDF 主体、mbedTLS | Apache-2.0（WAMR 含 LLVM 例外） | 保留通知；mbedTLS 不选 GPL 分支 |
| wpa_supplicant | BSD-3-Clause | 仅采用原文件明确提供的 BSD 选项 |
| Vivante VGLite 双许可文件 | MIT | 保留文件中明确的选择依据 |
| FreeType | FTL | 随产品文档提供 FreeType 归属声明；不选 GPL |
| littlefs、LZ4、lwIP | BSD 系列 | 保留各组件声明 |
| zlib、libpng、JPEG 解码器 | 各自的 Zlib/libpng/IJG 或供应商许可 | 保留上游许可，不统一标为 MIT |
| Espressif 音频/JPEG 等专用二进制组件 | Espressif Modified MIT 或组件原许可 | 仅用于许可覆盖的 Espressif 产品；不得当成无平台限制的 MIT |
| GCC 运行库 | GCC 运行时例外 3.1 | 核实工具链来源与例外适用范围 |

LVGL 附带的 FrogFS 和 ThorVG 内部实现包含 MPL 代码，系统构建排除其源文件；
不能仅靠链接器删除未使用函数。若需要相应功能，先实现独立的许可兼容替代。

`tools/licensing/check_system_licenses.py` 检查 SPDX 表达式和源码许可声明。
非 SPDX 双许可选择以 `reviewed-alternatives.json` 中的文件 SHA-256 固定；
升级后必须重新阅读实际声明，不能自动刷新哈希。新增或更新依赖还须核实来源、
完整许可证、传递依赖、编译输入及实际链接的二进制库。扫描通过不等于能识别
没有版权声明的复制代码，代码审查仍是必需步骤。禁止删除版权头来通过检查。

许可证选择依据：[FreeType 许可](https://freetype.org/license.html)、
[GCC libstdc++ 许可](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html)、
[GCC 运行时例外说明](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html.en)。
