# PXA UI 主题 ABI 0.4

应用向 UI service (3) 发送 `PXA_UI_THEME_GET` (11)，空请求体、非零请求 ID；响应是 `status:i32` 后接 60 字节主题快照。已绑定 UI 的应用在主题变化时接收 `PXA_UI_THEME_CHANGED` (0x8006)，请求 ID 为零，正文直接是同一快照。应用可使用 Guest C SDK 的 `pxa_ui_theme_get()`、`pxa_ui_parse_theme_event()`。

快照按小端编码：`generation:u32 | color_scheme:u8 | reserved:u8[3] | rgba:u32[10] | typography_px:u16[6]`。颜色是 `0xRRGGBBAA`，顺序为背景、表面、主色、主色上的文字、正文、次要文字、描边、成功、警告、危险。字号顺序是 caption、label、body、title、headline、display，默认分别为 12、14、16、20、24、28 像素。配色支持任意色板，不仅仅是明暗两种；`color_scheme` 仅表示当前亮/暗的对比度环境。收到变化事件后，自行绘制或缓存颜色的应用应重新绘制；使用 UI 主题色 token 的节点由 Host 更新。

系统设置的“主题色”可以在蓝色、青绿、紫色、琥珀色之间切换，明暗模式与主题色独立；主题服务更新后桥接器将完整色板传给 PXA Host。尚未实现主题色持久化，重启后恢复系统默认配色。
