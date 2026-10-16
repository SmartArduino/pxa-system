# 内置键盘统一验证（2026-10-15 阶段记录）

原生系统编辑器、PXA 应用选择内置以及 Provider 故障回退，统一使用同一个 `pxa_lvgl_ime` 手机键盘前端。原生封装仅映射主题、DPI、形状、四角及安全边距，不再同时创建隐藏 LVGL 键盘或另一套九键 UI。不新增 Guest ABI、帧缓冲、线程或常驻词库。

统一阶段修正了共享 ownership marker 引起的轮询问题：系统自己的原生键盘先检查本地所有权，避免下一次 poll 将其误认为外部托管编辑器并立即关闭。同步提交期间正文可能被删除或转为密码，回复生命周期和会话失效流程也有回归覆盖。

## 原生受控 A/B

被冻结的旧原生封装与当前前端使用同一 LVGL、显示配置、字体及 malloc/calloc/realloc/free 请求字节跟踪。每个进程第一次初始化单独记录，再重开 8 次，关闭后测回收；这是请求字节，不含分配器头、实际设备整机堆或 TTF 缓存。

| 形状 | 旧打开稳定增量 | 新打开稳定增量 | 旧暖峰值增量 | 新暖峰值增量 | 两版关闭差额 |
|---|---:|---:|---:|---:|---:|
| 圆角矩形 | 43,752 B | 5862 B | 50,960 B | 10,282 B | 0 |
| 圆形 | 43,752 B | 5902 B | 49,659 B | 10,060 B | 0 |

首次初始化的绝对峰值另存原始 JSON，不能与上述相对暖峰值混用。旧封装及复现程序在 `benchmarks/20261015-builtin-ime/`；本次扩字表后重跑结果仍与上表一致，见 `benchmarks/20261015-ime-characters/native-memory.json`。

```sh
python3 docs/zh-CN/benchmarks/20261015-builtin-ime/run-native-memory.py \
  --build /path/to/ninja-build --output /tmp/ime-memory
```

## 阶段验证与边界

当时原生 9 项回归、原生无 PXA/runtime 配置 5 项回归、ASan/UBSan/LSan，以及两种 Provider × AOT/Wasm × 13 种屏幕配置和 6 个故障包通过。pai-touch 的内置、完整和轻量 Provider 都实际提交「你好」，各反复开关 8 次，测试结束恢复选择并卸载 fixture。原始记录见 [阶段数据](benchmarks/20261015-builtin-ime/test-summary.json)。

用户曾报告显示异常，串口当时仍响应，重启恢复且用户确认正常；未得到 SPI 故障的确定根因，不能将其宣称为已证明并修复的 SPI bug。设备共享后使用明确的任务交接，避免两边同时操作串口。

这一阶段仍使用小字表；随后已实现完整规范字表、简繁裁剪、工具页光标和简中/英文精简 TTF。最终源码、固件、字体、当前真机内存与显示结论以[常用汉字与字体裁剪验证](input-methods-character-coverage-20261015.md)为准，阶段数据只作为冻结的历史基线。
