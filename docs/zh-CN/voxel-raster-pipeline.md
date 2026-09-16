# Voxel Craft Host Raster 迁移设计

## 目标与边界

Voxel Craft 的世界模拟、可见块选择、greedy mesh、背面剔除、视锥裁剪和投影仍在 Guest。Host 只接收已经投影到 Surface 像素坐标的紧凑 DrawList，在 Host-owned RGB565 buffer 上执行清屏、平面色 Quad、INDEX8 纹理 Quad 和能力受控的 additive sprite，然后把完成的 buffer 交给现有 Surface latest-frame mailbox。

Raster 不是另一套窗口或显示服务。Surface 继续拥有 buffer 生命周期、前后台帧、系统 UI 接管、缩放、旋转和 panel 提交；Raster 是 Surface 的可选生产 profile。这样权限框、锁屏和 Toast 仍沿用现有合成规则，也不会让 Raster kernel 获取 LVGL 锁、等待 TE 或等待 SPI。

## 旧链路

```text
Guest update
  -> 每像素 3D DDA ray cast
  -> Guest RGB565 framebuffer
  -> GuestMapped Surface buffer
  -> PRESENT(index)
  -> Host latest-frame mailbox
  -> 2x/4x nearest + rotate + byte swap
  -> panel
```

当前路径已经避免了每帧通过 `pxa_io` 复制完整 framebuffer，也已经有三缓冲和异步 presenter。主要瓶颈已经从传输变成 Guest AOT 中每像素的 DDA、纹理采样、雾化和实体深度测试。148x120 是 17760 条射线；每条射线又包含多次分支、除法和随机内存访问。降低分辨率能减少计算，却同时降低 HUD 和文字清晰度。

## 新链路

```text
Guest update
  -> 按 chunk revision 更新 greedy mesh cache
  -> chunk/face back-face + fog distance + frustum culling
  -> project and depth-sort visible quads
  -> compact Raster DrawList
  -> pxa_io(surface, RASTER_SUBMIT, list)
Host Surface backend
  -> validate complete list and capabilities
  -> copy compact list into a two-slot latest-frame mailbox
Presenter task
  -> acquire free Host-owned RGB565 buffer
  -> execute native raster kernels
  -> nearest upscale + rotate + byte swap + optional overlay
  -> panel
```

DrawList 提交只做完整校验并把紧凑命令复制到预分配 mailbox。它不执行 raster、不进入 LVGL、不等待 display DMA/TE/SPI。显示侧消费 mailbox 时才执行 native raster；新提交会替换尚未开始 raster 的旧列表并统计 dropped/replaced。三张 RGB565 buffer 仍由 Surface lease 状态机管理。

## Surface 0.3 Raster profile

RGB565 Surface 通过 `PXA_SURFACE_FLAG_HOST_RASTER` 请求 Raster profile。它与 `GUEST_MAPPED` 互斥。Host 不支持时在 `CREATE` 返回 `UNSUPPORTED`，Guest 随即回退到 0.2 GuestMapped 路径；再旧的 Host 则会把未知 create flag 拒绝，同样可以回退。`pxa_surface_write_frame` 保持不变。

Surface state 增加以下能力位：

- `HOST_RASTER`：资源上传和 DrawList 提交可用；
- `RASTER_TEXTURED_QUAD`：INDEX8 纹理 Quad 可用；
- `RASTER_ADDITIVE_SPRITE`：sprite 的逐通道饱和加法可用。

Guest 只能在 state 中存在相应能力时设置 `required_capabilities` 或 additive flag。Host 在执行任何命令前验证整份列表，避免半帧更新。

### 资源上传

资源通过 Surface handle 的 `PXA_SURFACE_IO_RASTER_UPLOAD` 上传并绑定到 Surface 生命周期。首版固定 16 个 texture slot 和一个 256 色 RGB565 palette：

- `PALETTE_RGB565`：正好 256 个 little-endian canonical RGB565；
- `TEXTURE_INDEX8`：row-major INDEX8，宽高和 payload 长度必须完全一致；
- 第一帧提交前，同一 slot 的成功上传原子替换旧资源；失败时旧资源仍有效。首帧后资源被冻结，避免异步 raster 与资源释放竞态。

ESP backend 把资源、一个与逻辑 Surface 同尺寸的 16-bit reciprocal-depth scratch buffer 和两个 48 KiB DrawList mailbox 放入 PSRAM。depth scratch 由串行 Raster consumer 复用，不跟随三张颜色 buffer 重复分配。DrawList 执行期间不分配内存，关闭 Surface 时统一释放。

### DrawList

Header 包含 magic、ABI major/minor、总字节数、required capability、command count、frame ID 和 flags。每条记录都有 type 和 record size；首版 Host 对未知记录拒绝整帧，未来记录必须同时增加 capability 后才能协商使用。首版记录为：

- `CLEAR_RGB565`；
- `FLAT_QUAD`；
- `TEXTURED_QUAD`，顶点使用 12.4 屏幕坐标、12.4 UV、0..255 光照和 Q8 view depth；ABI 1.1 对 `u/z`、`v/z`、`1/z` 做透视校正。Host 在扫描线覆盖区间内以 8 像素小段计算端点透视坐标并做 Q8 增量，避免 ESP32-S3 上逐像素软件整数除法；同一记录也可通过 solid-color flag 表示带深度的纯色场景面；
- `SPRITE`，仅在 capability 允许时接受 additive。

校验包括 header/version/长度/record count、已知 flags、required capabilities、slot 是否已上传、顶点与 UV 的数值范围、退化多边形、目标尺寸、frame ID 单调性以及每帧命令/字节上限。协议错误返回 `PROTOCOL_ERROR`，超限返回 `LIMIT_EXCEEDED`，缺能力返回 `UNSUPPORTED`，无空闲 buffer 返回 `WOULD_BLOCK`。

## Voxel mesh

每个 16x24x16 chunk 保留 revision。mesh cache 只在 revision 或 chunk 坐标改变时重建。每个轴的每个切片先生成“当前 voxel 非空气且相邻 voxel 不遮挡”的二维 mask，再把相同 block/face 的连续矩形合并成一个 Quad。跨 chunk 边界通过世界读取判断暴露面。当前最小闭环把水、玻璃和叶片也按不透明块处理；透明材质分层尚未完成，不能把它计入首阶段验收。

每帧先对 chunk 包围盒做 fog-distance 和视锥粗裁剪，再对候选 Quad 做背面剔除。与 near/far/四个视锥侧面相交的 Quad 使用 Sutherland-Hodgman 裁剪；三角形和五边形以上的结果以退化 Quad/triangle fan 发送，不能因为单个顶点越过 near plane 而丢弃整个面。Host 使用 reciprocal depth 做逐像素遮挡；ABI 1.1 的不透明面按近到远提交以尽早拒绝被遮挡像素，无深度 fallback 才保留远到近顺序。High/Balanced/Performance 当前分别保留最多 620/480/320 个候选面；缓存或 DrawList 达到容量时按质量档位截断，并在 Guest stats 记录 clipped/dropped Quad。

## 分辨率与质量

Raster Surface 的逻辑分辨率独立于 mesh 密度。High/Balanced/Performance 继续控制雾距离、可见 chunk 数、最大 Quad 和像素覆盖预算，Host 用 nearest-neighbor 放大。质量控制同时观察：

- Guest update、mesh rebuild、cull/project/sort 时间；
- DrawList bytes/command count；
- Host raster 时间和 covered pixels；
- Surface `WOULD_BLOCK`/queue wait、present 和 dropped frame。

只有 Guest 几何阶段超预算时才缩短视距或 Quad 预算；Host raster 超预算时先降低 pixel coverage/resolution；buffer wait 高时不继续降几何质量，而是报告显示消费瓶颈。升级和降级都使用连续窗口与迟滞。

## 内存预算

以 148x120、3 buffers 为默认：Host RGB565 buffers 约 104 KiB；reciprocal-depth scratch 约 35 KiB；256 色 palette 512 B；15 个 16x16 INDEX8 tile 3.75 KiB；两个 48 KiB DrawList mailbox；Guest chunk mesh cache 使用固定上限。296x240 High 的三个 Host buffers 约 416 KiB，depth scratch 约 139 KiB，只在实时 telemetry 证明预算允许时使用。

旧 GuestMapped fallback 仍保留现有约 450 KiB 的 Guest 静态 framebuffer/depth/预计算表。完成 Raster 稳定性验证后可把这些 fallback buffer 放入单独构建 profile，但首个兼容版本不删除。

## 前台缓冲与截图

Raster 只写 Surface backend 选出的空闲 buffer。presenter 完成消费前该 buffer 不可重用；最新 pending buffer 也不是截图源。截图和像素采样必须从 presenter 已确认完成的 foreground/current buffer 获取。系统模态 UI 出现时 direct scanout 暂停，旧 foreground 可用于 LVGL 合成；模态消失后仍要求一帧新的完整 Surface 才恢复 direct scanout。

## 验证与风险

Host 单元测试覆盖完整列表先校验后执行、裁剪、UV、RGB565、能力回退、buffer 替换/释放和 telemetry。Simulator 使用相同 ABI、kernel 和 ownership 语义，仅用于正确性；不能用其 FPS 推断 ESP32-S3。

主要风险是透明块仍暂按不透明处理、全屏 reciprocal-depth scratch 的 PSRAM 带宽、mesh rebuild 的瞬时峰值，以及生物/粒子目前使用低成本 billboard，细节不及旧像素路径的 box ray intersection。真机 profile 后再决定透明分层、实体贴图以及是否需要改成 tile depth。

性能数字必须标记来源。2026-09-16 在 ESP32-S3（pai-touch）实测：固定 4x 在连接 `pxadb logcat` 时为 14.2--14.6 fps，Host raster 约 23 ms，DrawList 约 19.5 KiB；固定 1x 在不连接日志时 HUD 约 8 fps。串口日志会与运行时 RPC 争用并显著压低 1x 可见帧率，因此不能把 logcat 期间的 FPS 当作实际交互帧率。
