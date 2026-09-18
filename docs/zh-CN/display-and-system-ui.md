# 显示配置与标准系统 UI

[English](../display-and-system-ui.md)

## 合约边界

`pxsys_display_profile_t` 以 renderer 无关方式描述逻辑分辨率、密度、刷新率、
safe insets、外形、圆角和 cutout。平台适配把窗口、折叠或硬件模式变化转换成
display service 更新；核心不包含 LVGL 对象或 SDL handle。

## 参考 UI 分层

- `ui/reference` 用 C99 计算响应式区域与 launcher grid。
- `ui/reference/lvgl` 负责 LVGL 呈现。
- 未来 Qt、Skia 或厂商 renderer 复用相同布局、主题和 display service。

Home 与 Settings 是普通 Native 应用，以完整身份注册并声明可替换角色。launcher
枚举同一 app registry，Native 与 PXA 不使用不同命名空间。

## 替换与裁剪

Home、Settings、status bar、navigation bar 和 notification shade 可独立裁剪。
Wi-Fi、蜂窝、数字电量、gesture handle、task switcher 模式和预览数量都有 CMake/
Kconfig 选项。产品也可注册高优先级 Native 或 PXA 角色 provider；替换呈现不会
fork 应用模型。

## 平台职责

硬件 port 在创建系统前提供实际 profile，并从 UI owner thread 发布模式变化。
板级几何留在产品层。safe inset、系统栏高度和内容 padding 是三个独立概念，
不能重复预留。手势导航模式下，窗口快照的 `system_bar_insets` 同时包含底部
Home 手势条和左侧 Back 手势条，Guest 应用据此把可交互控件放在系统手势区之外；
`pxsys_reference_layout_gesture_strip_height()` 与
`pxsys_reference_layout_back_gesture_width()` 是唯一的手势条几何来源。

## System chrome 与导航

状态栏和控制中心只读取 `pxsys_system_status_snapshot_t`，所有 toggle/level 操作
通过标准 service 回调产品。按钮模式提供 Back/Home/Recents；手势模式提供边缘
Back、底部 Home 与暂停进入 Recents。两者进入同一 Task/Runtime 合约。PXA 应用
先收到 `PXA_WINDOW_BACK_REQUESTED`；未消费时系统关闭顶层 task，并有限制防止
应用永久拦截 Back。
