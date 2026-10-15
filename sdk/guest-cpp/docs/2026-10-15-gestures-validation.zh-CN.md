# 2026-10-15 C++ SDK 手势迁移验收

浏览器同时存在另一组脚本引擎改动。本轮保留原工作区全部改动，以
`local/gesture-sdk-20261015/browser-validation-source` 独立快照验证手势；
基础版本和补丁保存在同目录的 `browser-validation-source.json` / `browser-gestures.patch`。
本报告的浏览器资源上限、三架构包和测试结果仅针对手势版本，未验证并行脚本版本。

本轮将应用里的点击、按住计时和拖动取消统一为 C++ SDK `PressGesture`。
SDK 原来已有 `PointerCapture`、`on_click` 与原始 `on_pointer`，没有通用的长按识别器。
现在还可直接组合声明式 `.on_click(tap).on_long_press(hold)`，固定文字、状态/Computed
文字及带样式按钮使用同一套语义；接口说明见 SDK 的 `GESTURES.zh-CN.md`。
Host/Core ABI 不变，继续使用现有指针和帧钟服务。

长按达到阈值只触发一次，UP 不再执行点击。移动超过逻辑像素容差后不可通过移回
恢复点击，CANCEL、后台和场景切换清理捕获。身份包括 pointer/surface/node/generation。
成功的属性更新保留捕获，失败的提交保留旧页面；SDK 的条件视图、Refresh、
稳定键列表、虚拟列表和导航页管理手势生命周期。

## 应用变化

- 阅读器 0.2.5：书架长按保持 550ms，目录和正文为普通点击；使用已有帧钟。
  移除了应用自行维护的按下时间、坐标及拖动哨兵，避免多根手指或整数边界误判。
- 浏览器 0.1.1：移除收藏网站、带链接图片预览、主工具栏/图片工具栏提示保持 500ms，
  均在按住期间触发。保存按下目标，释放不穿透到重绘后的其他项目。
  直接使用输入/帧钟时间戳；此前典型短按判断经过一次 DOWN 和四次 UP 的
  `clock_gettime`，现在手势判断内不再读取系统时间。这是代码路径结论，未据此推算 FPS。
  返回、导航、编辑、后台和屏幕变化清理待触发状态。
- 检索其他应用后，voxel-craft 连续挖矿已在更新循环推进；jump-jump-3d 的松手起跳
  是蓄力机制；pixel-dungeon 等为点击/拖动。没有发现还需要把长按判断从 UP 搬走的路径。
  输入法的键盘按住行为由 Host 管理。本轮未改变这些游戏及输入法的交互。

## 验证

原生覆盖阈值精度、只触发一次、UP 补发、拖出再移回、CANCEL、多指和身份隔离、
坐标/时间边界、无帧回调应用的临时帧钟、后台、属性 PATCH、失败提交、子树切换、
列表重排/回收、导航、动态标签与零堆分配；另运行 ASan/UBSan。全 SDK 回归结果见 JSON。

阅读器实际 Host 触摸：

| 配置 | 长按可见观察值 | 结果 |
| --- | --- | --- |
| 296x240-160 | 570.0ms | 按住即打开、UP 不穿透、点击/拖动/取消均通过 |
| 480x480-320-round | 570.7ms | 按住即打开、UP 不穿透、点击/拖动/取消均通过 |
| 240x240-160-round | 565.2ms | 按住即打开、UP 不穿透、点击/拖动/取消均通过 |
| 800x480-160 | 570.7ms | 按住即打开、UP 不穿透、点击/拖动/取消均通过 |

同时遍历书架、书源和目录所有页，尾页返回不跳项。四种配置的 Guest 线性内存峰值仍为
3 页（192KiB），协程池峰值仍为 8，分配失败为 0，与前一轮相同。
正式无插桩阅读器回放 37 次页面，正文像素参考、进度和缓存全部一致。

浏览器在 296×240/160dpi 和 480×480/320dpi 圆屏通过网站添加、去重、无效网址、
**松手前已移除网站**、UP 不穿透、保存/重启、拖动滚动条、音量键翻页及按住重复。
带链接图片在松手前打开预览，UP 不导航；缩放、拖动、适应、返回和后续短按链接正常。
正文像素比较排除了暂时状态提示；另有针对正式包的无联网、无 trace 长按检查。

SDK 示例没有 `on_frame`：框架仅在有待触发长按时临时启用最多 16ms 帧钟，触发或取消
后恢复原周期；无游戏循环的应用恢复为 0。示例通过实际屏幕检查了按住反馈、
一次触发、松手不点击、点击、拖出再移回及取消。

桌面观察时间包含注入、调度、UI、显示及截图轮询；构建期间有 CPU 竞争。
这些值不能作为真机输入延迟，也未测量游戏 FPS 改善。未刷写或改动设备。

## 内存与资源

识别器原生实测 40B，内联存储，无堆分配或协程。迁移自手写状态时会有几十字节
固定状态差异；不能把“无堆分配”表述成“功能完全免费”。普通按钮不携带识别器。
原生 x86_64 的普通 Button Page / 带样式 Button Page / 普通按钮 Refresh Page，
修改前后均为 **144 / 184 / 480B**。仅启用长按的页面推导并保存固定容量注册表。
导航增加每路由只读方法表指针及周期状态查询，不预留每路由手势缓存。

阅读器/浏览器的最大 Guest 线性内存配置仍分别为 2MiB/512KiB。
未增加帧缓冲、深度缓冲或命令缓存；原生及模拟器验证不替代 SRAM/PSRAM 真机峰值测量。
按钮文字自身及用户回调仍可分配，零分配结论针对识别器、调度和测试中的短标签路径。

## 复现

源码工作区：

```sh
bash deps/pxa-system/tools/package/test_guest_cpp.sh
SANITIZE=1 bash local/pxa-apps/novel-reader/tests/test.sh
bash local/pxa-apps/browser/tests/test.sh
python3 local/pxa-apps/novel-reader/tests/build_playtest.py --target simulator \
  --output local/gesture-sdk-20261015/reader-qa
python3 local/pxa-apps/novel-reader/tests/interaction_probe.py \
  --package local/gesture-sdk-20261015/reader-qa/artifacts/pxa-novel-reader.pxa \
  --output local/gesture-sdk-20261015/reader-sim
bash tools/app.sh build novel-reader --target simulator,esp32s3,esp32s31 --aot-only \
  --output local/gesture-sdk-20261015/reader-release
bash tools/app.sh build browser --target simulator,esp32s3,esp32s31 --aot-only \
  --output local/gesture-sdk-20261015/browser-release
```

浏览器使用 `tests/fixture.py` 本地服务，带 `PXA_BROWSER_TRACE=1` 及
`PXA_BROWSER_START_URL=http://127.0.0.1:31878/` 构建后运行 `tests/ui_smoke.py`；
图片用 `/image-linked` 构建后运行 `tests/image_smoke.py --case linked`。
正式包使用 `tests/release_gesture_smoke.py`，不需要 trace 或网络。
这些脚本的 `--help` 给出模拟器、签名公钥、IME 包及输出目录参数。
SDK 示例及其模拟器脚本在 `sdk/guest-cpp/examples/gestures/`。
本轮日志、包、截图和机器结果位于工作区 `local/gesture-sdk-20261015/`；
这里的 JSON 保存摘要及最终包校验值。应用构建保持 O3，包含三架构 AOT，未上传商店。

最终：63 组主原生测试与动态国际化、3 种协程池配置、直接控制及 Python 生成/诊断检查均通过，
日志末行为 `PXA C++ Guest host tests OK`。浏览器两个 UI 配置均额外通过按住期间返回取消，
链接图片预览通过。结果与包校验值见 [JSON](2026-10-15-gestures-validation.json)。
浏览器复现使用上述快照时，构建加
`--source-root local/gesture-sdk-20261015/browser-validation-source`；原工作区的并行脚本引擎未回滚或合并。
