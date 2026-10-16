# GameRender 应用级输入

此示例没有 `view()`、Canvas、绘图资源或 UI 控件。方向键移动方块，A 或触摸/鼠标按下切换颜色，B 复位位置；顶部 12 格显示完整按钮状态。

`on_controller(Context&, const input::ControllerState&)` 和
`on_pointer(Context&, const input::Pointer&)` 由 AppRuntime 自动订阅和路由。
输入回调更新状态，`on_update` 处理持续移动，`on_frame` 只负责 GameRender 绘制。
后台清空按键，避免松开事件丢失后持续移动。

在 pxa-projects 中构建 DOIT GameKit 包：

```sh
tools/app.sh build game-input --board doit-gamekit \
  --source-root deps/pxa-system/sdk/guest-cpp/examples --aot-only
```

底层复用 UI 服务的输入根节点。该节点透明、无子节点，不创建 Canvas 或绘图缓冲；包需声明 `ui` 服务。现有固件无需升级输入协议。
