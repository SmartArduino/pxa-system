# ABI 控制请求微基准

每个窗口输出 32 条 `ABI_BENCH`，每条在两个单调时钟读取之间发出 1000 次
有效的未知请求取消，预期全部成功且没有完成事件。计时区间不记录日志、不分配内存。
这衡量 SDK 编码、WAMR 边界与 Host Core 分派总成本，不能代替游戏显示 FPS。
旧 Host 会在适配器中提前返回未知 token，新 Host 使用统一 Runtime 校验；
结果只代表这项完整操作，不代表所有服务或纯导入指令的成本。

默认包声明 Core 1.0，使用 `pxa_submit`。复制整个示例目录后，将 package.json 的
包 ID 改为另一个测试 ID，并将 min_sdk、target_sdk、compile_sdk 都设为 `[1,1]`，
即可通过相同构建入口生成 `pxa_call` 变体；不要只定义宏而不提高最低版本。
前后台切换开始新窗口，后台停止时钟；停止、重启可重复测量初始化。

在 pxa-projects 中构建并部署：

```sh
tools/app.sh build abi-control-bench --source-root deps/pxa-system/sdk/guest-cpp/examples --target esp32s3,simulator --output local/abi-bench
tools/pxadb/pxadb.py package install local/abi-bench/pxa-abi-control-bench.pxa --port /dev/ttyACM1 --yes
python3 tools/measure-device-abi.py --port /dev/ttyACM1 --app pxa-cpp-abi-control-bench --package local/abi-bench/pxa-abi-control-bench.pxa --output local/abi-bench/results
```

每次新启动及恢复前台分别重置 ESP-IDF 分配最低值，记录当前占用和该窗口的真实峰值。
不要将历史 WAMR/资源峰值加到当前堆占用上：Guest 线性内存、映射和分配器统计可能重叠。
测试结束会停止测试应用、结束堆监测并回到 HOME，不修改性能浮层或日志设置。
