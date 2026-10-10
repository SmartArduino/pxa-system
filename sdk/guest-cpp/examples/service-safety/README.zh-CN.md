# 服务生命周期与真实 AOT 回归

使用可选的 UI、FS、Clock、Device、Surface、GameRender、Permission 与 Audio 服务，验证异步任务的参数快照和资源移动。此例不增加其他应用的默认能力、内存或清单权限。

启动后自动运行核心检查；`Repeat checks` 再运行一次，`Test audio` 验证音频打开、EQ 参数快照、移动后的查询和 flush，并播放一个短而低音量的测试音。权限被拒绝时显示错误，不擅自修改系统权限。文件只使用自身私有目录中的 `round-trip.bin`，成功后移除。

映射 Surface 必须声明 pinned linear memory；示例固定上限 131,072 B，与其初始两页相同，避免内存增长后 Host 保留的像素指针失效。这不是所有应用的默认配置。Surface 很小且隐藏；GameRender 仅验证一次提交及重复提交错误。测试数据不能用来声称实际游戏 FPS 提升。

构建并运行完整模拟器测试：

```sh
# 在 pxa-system 仓库中，使用已有模拟器构建目录。
cmake --build /path/to/simulator --target pxsys_declarative_ui_test
WAMRC=/path/to/wamrc WASI_SDK_PATH=/path/to/wasi-sdk-34 \
  bash tools/package/test_cpp_service_safety.sh /path/to/simulator /tmp/service-safety-captures
```

脚本使用开发签名钥匙的公钥验证包；可通过 `PXA_SIGNING_KEY` 指定自有钥匙。私钥不会进入报告或测试包。三个真实 AOT 配置是 240×320/160 DPI、480×640/305 DPI 和 454×454 圆形/305 DPI，每个配置都检查核心/音频两次、前后台切换和重复启动。屏幕安全边距通过 SDK `SafeArea` 处理。

原生回归与 ASan/UBSan：

```sh
bash tools/package/test_guest_cpp.sh
clang++ -std=c++2c -O1 -g -Wall -Wextra -Werror -Wno-attributes \
  -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I sdk/guest-cpp/include sdk/guest-cpp/tests/service_lifetime_test.cpp \
  sdk/guest-cpp/src/{runtime,game,surface}.cpp -o /tmp/service-lifetime
ASAN_OPTIONS=detect_stack_use_after_return=1:detect_leaks=1 \
  UBSAN_OPTIONS=halt_on_error=1 /tmp/service-lifetime
```

HTTP/IPC 重叠编码的独立回归在 `tests/encoding_alias_test.cpp`，链接 `src/{runtime,net,ipc}.cpp`；编译诊断在 `tests/test_service_diagnostics.py`。

2026-10-11 的前后结果与基准程序见 [validation/20261011](validation/20261011)，总说明见 [SDK_RELIABILITY.zh-CN.md](../../SDK_RELIABILITY.zh-CN.md)。本轮原始工作目录是项目根的 `local/sdk-audit-20261011`，包含修改前完整 SDK、原生测试日志、独立导出 SDK、所有目标包与完整 Host 日志；仓库仅保留小型可复现数据与截图。

仓库存档日志规范化为 LF 并移除行尾空白；测量值和日志内容保持不变，原始串口字节保留在上述工作目录中。

性能复现先为修改前/后各提供完整 `guest-cpp` 目录，再使用 `validation/benchmark_fs.py --before ... --after ... --output ...`；它以相同 O3、关闭 exceptions/RTTI/builtins 的参数交替执行同一工作负载，校验导入次数、字节摘要及任务池占用。测的是原生 Guest 编码/分派成本和相同模拟导入，真实存储或网络延迟应另测。

[三种屏幕配置的实际 AOT 截图](validation/20261011/profiles.png)

设备复现使用项目工作区的 PXADB。先构建/安装临时 `service-safety` 包，并在第一次点击 `Test audio` 时批准该测试包的可选音频权限；下面的脚本只运行已安装的测试包，不安装、授权或修改设备设置。默认点击坐标对应本轮 296×240 pai-touch，其他布局使用 `--core-point x,y --audio-point x,y` 调整。

```sh
python3 sdk/guest-cpp/examples/service-safety/validation/run_device.py \
  --pxadb /path/to/pxa-projects/tools/pxadb/pxadb.py --port /dev/ttyACM1 \
  --output /tmp/service-device --repeat 3
```

检查每轮的 `core-*.png` 显示 `Core checks OK`、`audio-*.png` 和 `resume-*.png` 显示 `Audio checks OK`。权限对话框或错误文本应判为失败，不能只以脚本退出码判断功能成功。`device.json` 将启动和稳定阶段分开，`MEMORY START/STOP` 测本次局部最小空闲值，不能和历史累计峰值混用。测试后通过 PXADB 卸载临时包，清除其私有目录与权限。

本轮[设备服务截图](validation/20261011/device-services.png)、[六轮前后计数器截图](validation/20261011/device-counter.png)与原始 JSON 一并保留。计数器前后只更换 SDK，测试包采用独立 ID，不能覆盖原有应用；按 before/after、after/before、before/after 安装、启动、点击五次、截图、停止，阶段前后读取 `MEMORY` 并重置局部监测。它验证默认 SDK 占用与相同 UI 行为，不是游戏 FPS 基准。

[现有三个 C++ 游戏的启动截图](validation/20261011/game-smoke.png)来自使用新 SDK 的独立构建。它们通过启动、渲染和前后台切换检查；没有替代完整游戏回归或前后 FPS 对照。
