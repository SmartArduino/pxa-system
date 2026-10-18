# Clock 0.3 真机测试

此诊断应用要求 Clock >=0.3 和 `timing` 能力，不在旧 Host 上回退到未支持的参数。
它查询设备范围，在 1ms、8ms、33ms 各采集128个真实 tick 间隔；随后请求999µs，
验证错误后33ms订阅继续投递，再验证60s配置被接受和取消。60s阶段不会等待整整一分钟。
每组128个采样在结束后汇成一条统计日志，避免日志突发丢失或影响采集间隔。

构建：

```sh
tools/app.sh build clock-timing --board doit-gamekit --target esp32s31 \
  --source-root deps/pxa-system/sdk/guest-c/examples --output local/clock-test --aot-only
```

使用 PXADB 安装并运行 `pxa-clock-timing`，读取 `CLOCK LIMITS`、`CLOCK RESULT`
和 `CLOCK TEST PASS` 日志。测试期间保持前台和屏幕唤醒；不把空载 tick 间隔当作游戏帧率。
应用使用固定缓冲区，不分配堆内存，不写用户数据。
