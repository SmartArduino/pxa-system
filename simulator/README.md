# Simulators / 模拟器

PXA System ships two product-independent host simulators.

PXA System 提供两个不依赖产品代码的主机模拟器。

- `headless/` validates composition, ownership, themes and renderer contracts
  in CI without a window.
- `desktop/` runs the actual standard system UI with SDL2 and LVGL, including
  light/dark themes, locale, viewport and navigation mode selection.
- `headless/` 在 CI 中无窗口验证组合、所有权、主题与 renderer 合约。
- `desktop/` 使用 SDL2/LVGL 运行真实标准系统 UI，支持明暗主题、locale、窗口
  尺寸和导航模式。

The product repository may retain a separate richer simulator for product
pages and device services. It is an integration consumer, not this portable
simulator's implementation.

产品仓库可以保留另一套包含产品页面和设备服务的完整模拟器；它是 PXA System
的集成使用者，不属于这里的可移植实现。

See [`desktop/README.md`](desktop/README.md) for build and runtime options.

构建与运行参数见 [`desktop/README.md`](desktop/README.md)。
