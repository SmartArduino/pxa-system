# 流式 HTTP 验证示例

这个示例验证真实网络、Guest ABI 和协程等待，而不是仅模拟返回值。需要支持 net 0.4 / Core 服务 0.2 的 Host；Core 导入仍然是 `pxa.core.v2`。

先启动固定数据源，确认终端已打印监听地址，再运行应用：

```sh
python3 simulator/desktop/tests/http_fixture.py --bind 0.0.0.0 --port 31877
```

模拟器使用 `http://127.0.0.1:31877`。设备使用电脑在同一局域网中的地址。构建时设置环境变量 `PXA_HTTP_FIXTURE_URL=http://电脑地址:31877`，或者在直接使用 CMake 时设置同名缓存选项。更换地址时重新配置构建缓存。

```sh
PXA_APP_SOURCE_ROOT="$PWD/sdk/guest-cpp/examples" \
PXA_HTTP_FIXTURE_URL=http://127.0.0.1:31877 \
PXA_SIGNING_KEY=/path/to/publisher-private.pem \
bash tools/package/package_app.sh http-stream simulator /tmp/http-stream
```

换用 `esp32s3` 构建设备包。安装并启动后，授予这个测试应用声明的 `net.client/web` 权限。测试地址应完全由自己控制；示例不会访问公共服务器。

日志应包含 `HTTP_STREAM ALL PASS`。覆盖内容：1 MiB 已知长度下载、1 MiB chunked 下载、重复 Set-Cookie、提前响应头、取消、100 KiB 上传，以及没有提供完整上传正文时服务器提前返回 413。所有下载逐字节检查固定内容；每次传输正文缓冲为 4096 字节，双向传输合计仍为 4096 字节。Guest 线性内存固定为 128 KiB。

屏幕显示当前阶段和最终结果，应用私有目录的 `result.txt` 保存本轮结果及启动时间。结果文件在完成时关闭，便于独立读取；不要因为调试日志队列丢失一条完成消息而判定网络失败，也不要误用上次运行的结果。

构建时设置 `PXA_HTTP_TEST_CASE=buffered` 或 `streamed`，可对比同一个 256 KiB 数据源。两个对比包保留相同代码路径、UI、文件记录和 128 KiB 线性内存，只改变选择的路径。使用不同的构建缓存目录，避免同时构建时覆盖输出。设置 `PXA_HTTP_TEST_CASE=paired` 可在同一次启动中交替运行旧、新路径，首对用于预热，随后各六次用于比较吞吐。该模式最终也输出 `HTTP_STREAM ALL PASS`，测量脚本使用 `--case full --repeat 1`。每轮分别记录响应头、首次正文及完成时间。数据源在响应头后固定等待 80 ms 才发送正文。

产品仓库的 `tools/measure-device-http.py --port PORT --case full --output DIR` 会重复启动三次，通过日志、结果文件和截图核对结果，并使用 `MEMORY START/STOP` 记录本轮真实分配峰值。`--case buffered/streamed` 用于对应的对比包。截图在内存统计窗口结束后抓取，不混入网络峰值；首次初始化应与后续启动分开列出。

正文总量和缓冲预算是独立的。4 KiB 缓冲不表示最多只能下载 4 KiB；应用仍通过 `max_body_bytes` 限制整个传输。读取返回暂时不可用时，SDK 挂起当前协程，通过资源等待唤醒；不会创建每块数据一个协程，也不会用游戏帧轮询网络。

原生完整边界测试可以运行：

```sh
ctest --test-dir build -R pxsys_desktop_http_stream_test --output-on-failure
```

它还覆盖慢消费者、接收缓冲满时取消、总量超限、重定向交回 Guest、矛盾的 HTTP 长度，以及同一个 256 KiB 数据源的旧缓冲接口和新流式接口对比。原生消费者和实际 Wasm 应用的调度不同，不能把两者的总耗时当作同一种工作负载比较。
