# 实现状态

[English](../implementation-status.md)

## 已实现并通过主机测试

- `pxa_system_core`：身份、registry、lifecycle、Intent、task、role、theme、renderer
  host、版本化 service registry 和有界 event broker。
- `pxa_system_native` 与 `pxa_system_pxa`：统一 runtime provider 边界；PXA binding
  使用 `libpxa`，caller principal 由 Host 注入。
- `pxa_system_standard`：可配置组合根；产品也可以单独组合各核心对象。
- headless/LVGL renderer、后端无关参考布局和 LVGL 标准系统 UI。
- 独立 `simulator/headless` 与 `simulator/desktop`；后者用 SDL2/LVGL 直接运行标准
  UI，从 `apps/pxa` manifest 生成启动器目录，并通过统一 Intent、task 与 PXA
  runtime 生命周期启动轻量模拟应用。它支持明暗/自定义主题、locale、显示形状、
  安全区、确定性状态与 PNG 截图，不要求 Guest 工具链。
- PXA UI 0.3、Manifest 0.6、package 签名/安装、Guest C SDK、Native C binding、
  Python 规范/golden vector 工具与应用 i18n 编译。
- `apps/pxa` 已纳入独立树；普通 UI 使用语义主题，Canvas 游戏保留内容 palette。
- WAMR 固定在 `config/wamr.json` 的提交；ESP patch 只生成 build overlay，完整
  memory-map override 明确替换，自动测试确认子模块保持干净。
- 顶层 CMake 的 51 项宿主机测试覆盖 headless/SDL2 模拟器 smoke/self test、
  296x240、390x844、454x454 圆屏的六项主题/locale 截图矩阵、独立 LVGL
  renderer 单测和 WAMR overlay 校验。

## 仍属于产品集成的工作

- 产品 publisher、角色声明与 service 访问策略。
- 已安装应用声明特权角色时的选择与持久化 UI。
- 当前 ESP WAMR Host 的物理执行槽仍为单实例；System runtime/role 模型本身支持
  多实例，未来需扩展产品 Host 才能同时运行两个可见 PXA 应用。
- 不要求跨 renderer 的旧产品页可以继续使用 Native LVGL 兼容模式；需要可移植性
  的页面应逐步转为 PXA UI transaction。

## 兼容性与销毁顺序

所有新合约仍为 draft `0.x`。销毁前必须停止应用和 pending service call；标准
组合按 role instances、tasks、roles、Intent、runtime、services、events、renderer、
theme、app registry 的依赖逆序释放。
