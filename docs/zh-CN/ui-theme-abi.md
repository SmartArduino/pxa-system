# PXA UI 主题 ABI 0.4

应用向 UI service (3) 发送 `PXA_UI_THEME_GET` (11)，空请求体、非零请求 ID；响应是 `status:i32` 后接 60 字节主题快照。已绑定 UI 的应用在主题变化时接收 `PXA_UI_THEME_CHANGED` (0x8006)，请求 ID 为零，正文直接是同一快照。应用可使用 Guest C SDK 的 `pxa_ui_theme_get()`、`pxa_ui_parse_theme_event()`。

快照按小端编码：`generation:u32 | color_scheme:u8 | reserved:u8[3] | rgba:u32[10] | typography_px:u16[6]`。颜色是 `0xRRGGBBAA`，顺序为背景、表面、主色、主色上的文字、正文、次要文字、描边、成功、警告、危险。字号顺序是 caption、label、body、title、headline、display，默认分别为 12、14、16、20、24、28 像素。配色支持任意色板，不仅仅是明暗两种；`color_scheme` 仅表示当前亮/暗的对比度环境。收到变化事件后，自行绘制或缓存颜色的应用应重新绘制；使用 UI 主题色 token 的节点由 Host 更新。

系统设置的“主题色”可以在蓝色、青绿、紫色、琥珀色、珊瑚、鼠尾草、玫瑰、石墨之间切换，明暗模式与主题色独立；主题服务更新后桥接器将完整色板传给 PXA Host。尚未实现主题色持久化，重启后恢复系统默认配色。

## 系统语义色

系统内部的 `pxsys_theme_snapshot_t` 采用参考 Material 3 的语义角色：主色及其容器色、次要色、第三色、错误色及对应的前景色，另有背景、表面、分层表面容器、轮廓、反色表面等。亮色和暗色各有一套预设颜色；设置中的八种配色会切换整套强调色，而不再只替换主色。新加入的珊瑚、鼠尾草、玫瑰、石墨还配有协调的背景和分层表面色。`pxsys_theme_snapshot_apply_palette()` 集中维护这些预设，`pxsys_theme_palette_name()` 提供稳定的内部名称。旧的系统色 token 编号保持不变；主色/前景色、表面文字和轮廓分别可通过 `PXSYS_COLOR_PRIMARY` / `PXSYS_COLOR_ON_PRIMARY`、`PXSYS_COLOR_ON_SURFACE`、`PXSYS_COLOR_OUTLINE` 别名访问。

Guest 应用的 0.4 版线缆格式**仍为上述 10 色 / 60 字节**，旧 token 编号不变。Host 的 LVGL 渲染器另外支持 `PXA_UI_THEME_SURFACE_CONTAINER_LOW` 至 `PXA_UI_THEME_INVERSE_PRIMARY`（10–31）：应用可通过 `pxa_ui_set_theme_color()` 引用这些语义色，切换主题后由 Host 自动更新，无须重新构建节点。`PXA_UI_THEME_ROLE_COUNT` 是渲染器色槽数量，不是快照中的色数。Guest 若要独立读取这些新角色以自行绘制，仍需要带版本协商的新快照 ABI，不能直接扩大现有快照。预设为静态配色，不包含 Android 的壁纸取色或动态 HCT 生成功能。
