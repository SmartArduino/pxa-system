# libpxa

[English](README.md)

`libpxa` 是 PXA Host Core 的平台无关 C99 实现，名称保持不变。核心不依赖
ESP-IDF、RTOS、文件系统、密码库、图形库、Wasm engine 或 C++。平台通过回调
表提供这些能力。

运行时由单一 owner thread 驱动且不可重入。初始化后的 Core 不主动分配内存；
调用者用 `pxa_runtime_limits_t` 选择容量并提供 workspace。注册 service 和借用的
workspace 必须活到 `pxa_runtime_deinit()` 返回。

主要能力包括：有界 wire codec、Component 生命周期、generation handle、异步请求
与取消、有界事件 mailbox、Window/Permission/IPC/Storage/FS/Sensor/Scheduler/
Net/Audio、PXA UI 0.3、package 校验和可恢复 slot transaction。

`adapters/` 是可选第二层：OpenSSL/LZ4/POSIX、WAMR engine 和 LVGL UI adapter。
WAMR 与 LVGL 是外部 build-tree 目标，不改变核心边界。PXA System 的 Native 与
PXA runtime 最终进入同一 identity、Intent、service 与 policy 实现。

```sh
cmake -S libpxa -B /tmp/libpxa-build
cmake --build /tmp/libpxa-build
ctest --test-dir /tmp/libpxa-build --output-on-failure
```

公共 ABI 规则见 [ABI.md](ABI.md)，内部依赖和所有权见
[ARCHITECTURE.md](ARCHITECTURE.md)，审计处置见 [AUDIT.md](AUDIT.md)。
