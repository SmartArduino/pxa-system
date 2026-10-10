# 国际化示例

这个原生 UI 应用演示启动语言、系统语言通知、英文/简体中文/繁体中文/俄文切换、
复数计数以及有界文本绑定。示例使用 24 dp 留边和 8 dp 间距，
让标题避开小屏幕上的系统栏；国际化模块不修改窗口或显示设置。“切换语言”按钮只覆盖应用的语言，不修改系统设置。

应用使用只读目录、`State<string_view>` 和 `State<TextBuffer<96>>`，更新路径没有动态分配。
长度不确定的实际业务文本可以直接使用 `Translator::format()` 和 `State<std::string>`。
打包时设置 `PXA_APP_DEFINES=PXA_I18N_DYNAMIC_TEXT=1` 可让计数文本改用动态字符串，
同一套测试覆盖两种模式。两种方式共享相同的类型检查、回退与格式化规则，详见 [SDK 文档](../../I18N.zh-CN.md)。

在 pxa-system 根目录构建：

```sh
PXA_APP_SOURCE_ROOT="$PWD/sdk/guest-cpp/examples" \
PXA_SIGNING_KEY="$PWD/apps/pxa/.dev-signing/publisher-private.pem" \
PXA_PACKAGE_OUTPUT_ROOT="$PWD/out/i18n-simulator" \
bash tools/package/package_app.sh i18n simulator "$PWD/out/i18n-simulator/pxa-i18n"
```

把目标替换为 `esp32s3` 或 `esp32s31` 可构建设备 AOT 包；签名密钥必须是设备信任的发布者。
独立 SDK 导出后也可使用相同例子和命令，完全不需要 Guest C SDK。

SDK 原生测试与真实 AOT UI 检查：

```sh
bash tools/package/test_guest_cpp.sh
bash tools/package/test_cpp_i18n_app.sh /path/to/build/pxsys_i18n_app_test
```

后者在真实 WAMR 和 LVGL 上检查四种语言的实际标签、计数按钮、语言事件缓冲区所有权、
前后台切换和三次完整启动；不依赖 mock 翻译输出。配置桌面模拟器测试时，也注册同名 CTest。
