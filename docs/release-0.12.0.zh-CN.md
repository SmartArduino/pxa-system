# PXA System / DevKit 0.12.0

- 新增 Video 0.3 服务，以及 Guest C 和 C++ 接口。应用可查询 Host 支持的格式和播放能力。
- 更新 Canvas 椭圆绘制、输入法、启动器和系统手势。
- 调整网络 URL 长度上限，保留各 Host 的能力查询与资源预算。
- 配套 Agent Kit 0.7.6 加入来自近期 PXA 游戏的渲染、资源制作、音频和复现测试流程。

Guest Core 继续使用 2.1，Host C ABI 与 AOT ABI 保持原有版本。应用需声明实际使用的服务及功能位。

## English

PXA 0.12.0 adds the Video 0.3 service and its Guest C and C++ interfaces. It improves Canvas ellipse drawing, native editing, the launcher and system gestures, and increases the network URL limit. Agent Kit 0.7.6 includes rendering and reproducible validation patterns from recent PXA games. Guest Core, Host C and AOT ABI identifiers are unchanged.
