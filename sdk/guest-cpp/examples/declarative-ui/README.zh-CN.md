# 声明式 UI 示例

展示显式 State 绑定、多个依赖合并的 Computed、动态按钮标签、启用标志、
组件局部状态、When、安全区和滚动，以及同步/异步 Action 的错误处理。
显示指标从启动配置读取，并接收后续通知；布局按 Host 密度缩放。
Details 分支含浮点文本绑定，用于验证 WASI libc++ 的实际编码路径。

在 pxa-system 根目录运行：

```sh
PXA_APP_SOURCE_ROOT="$PWD/sdk/guest-cpp/examples" \
PXA_SIGNING_KEY="$PWD/apps/pxa/.dev-signing/publisher-private.pem" \
bash tools/package/package_app.sh declarative-ui simulator out/packages/pxa-declarative-ui
```

签名密钥需使用自己的开发发布者密钥。独立 C++ SDK 包内也包含本示例；将
输出路径改为独立目录，并用 PXA_PACKAGE_OUTPUT_ROOT 指定其父目录即可构建。

自动测试调用 `tools/package/test_cpp_declarative_ui.sh <pxsys_declarative_ui_test>`，
它构建真实 AOT，创建临时存储，点击/滚动原生控件、切换前后台并重复启动。
设置 `PXA_SDK_TEST_ARTIFACT_DIR` 可保留显示缓冲区 BGRA 与指标 JSON。
[验证报告及截图](validation/REPORT.zh-CN.md)记录本轮实测。
