# 可选的游戏绘制与屏幕适配能力

这些能力没有默认帧缓存或堆容器，按需包含头文件。游戏帧使用调用方提供的命令缓冲，资源上传使用可复用的调用方暂存区。

- `game.hpp` 的 `Frame::palette_triangles(vertices, index)` 编码不带深度的纯色 painter 三角形批次。顶点 `depth_q8` 必须为零，`light` 选择合法的调色板光照行。适用于已排序的封闭等距场景；开放世界仍应使用深度测试。
- `game_upload.hpp` 的 `game::Upload(renderer, scratch)` 支持 INDEX8 纹理和 RGB565 调色板同步上传，检查尺寸、槽位、能力和暂存容量。允许源数据与暂存区重叠，返回后即可复用。纹理需要 `20 + width*height` 字节；调色板需要 `20 + 512*light_levels` 字节。上传完成后的资源内存由 Host 负责。
- 静态贴图优先在 `resources.json` 中声明，通过 `context.assets().load(AssetKind::texture, path)` 和 `renderer.bind_asset()` 绑定。这样无需把同一大图同时驻留在代码映射、Guest 线性内存和上传缓冲中。绑定后可释放临时 Asset，Renderer 保留资源。
- `ui_controller.hpp` 解码画布手柄事件并检查负载长度、连接状态及保留字段。应用自行保留上一次按钮位图来检测边沿。
- `ui_geometry.hpp` 的 `safe_rectangle(metrics)` 返回位于安全边距和圆角/圆形轮廓内的保守矩形；背景仍可铺满屏幕。`scale_display(metrics,w,h)` 将安全距离、圆角和像素密度一次性换算到渲染坐标。Canvas 指针先用现有 `surface_point()` 换成物理像素，再映射到渲染坐标，避免重复 DPI 缩放。

Host 为不透明、横向 1:1 的 INDEX8 sprite 提供专用内核。相邻同高、同源、同尺寸且横向连续的重复 sprite 批次会复用已经绘制的行，按倍增长度复制；不增加临时缓存。透明、缩放、不同源或裁剪不满足条件时保留通用路径。分行光栅化的输出与逐个绘制一致。

示例应用 `jump-jump-3d-cpp` 使用有限三角形批次、三档抗锯齿字体和抖动背景；`pixel-dungeon-cpp` 使用外置贴图、整数像素缩放、固定存档缓冲和 Host 音频时钟。两者的共享适配器位于 pxa-apps 的 `common/cpp/`，不属于所有应用的默认启动路径。

验证：运行 `tools/package/test_guest_cpp.sh`；新增 `game_arcade_test` 检查真实 Host 光栅结果、裁剪、分行与逐实例等价、上传重叠、零堆分配和屏幕安全区域。
