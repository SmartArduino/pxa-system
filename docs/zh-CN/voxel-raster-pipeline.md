# Voxel Craft GameRender 迁移设计

## 目标与边界

Voxel Craft 的世界模拟、可见块选择、greedy mesh、背面剔除、视锥裁剪和投影仍在 Guest。Host 只接收已经投影到屏幕坐标的紧凑 DrawList，在 Host-owned RGB565 buffer 上执行清屏、平面色 Quad、INDEX8 纹理 Quad 和能力受控的 additive sprite，然后把完成的 buffer 交给现有显示 presenter。

GameRender 是独立的游戏渲染服务，拥有持久纹理、DrawList mailbox 和渲染 context；Surface 只保留完整像素帧的 stream/GuestMapped 语义。两者在 ESP backend 内共享前后台帧、系统 UI 接管、缩放、旋转和 panel presenter。这样权限框、锁屏和 Toast 仍沿用现有合成规则，也不会让 raster kernel 获取 LVGL 锁、等待 TE 或等待 SPI。

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
  -> pxa_io(game_render_context, SUBMIT, list)
Host GameRender backend
  -> validate complete list and capabilities
  -> copy compact list into a two-slot latest-frame mailbox
Presenter task
  -> acquire free Host-owned RGB565 buffer
  -> execute native raster kernels
  -> nearest upscale + rotate + byte swap + optional overlay
  -> panel
```

DrawList 提交只做完整校验并把紧凑命令复制到预分配 mailbox。它不执行 raster、不进入 LVGL、不等待 display DMA/TE/SPI。显示侧消费 mailbox 时才执行 native raster；新提交会替换尚未开始 raster 的旧列表并统计 dropped/replaced。三张 RGB565 buffer 仍由 Surface lease 状态机管理。

## GameRender 0.1

游戏画面通过 `PXA_GAME_RENDER_CREATE_CONTEXT` 创建独立 context。Host 不支持时返回 `UNSUPPORTED`，Voxel Craft 随即回退到 Surface 0.2 GuestMapped 路径。`pxa_surface_write_frame` 和 mapped buffer ABI 不承担任何绘制命令。

创建结果返回以下能力位：

- `TEXTURED_QUAD`：INDEX8 纹理 Quad 可用；
- `ADDITIVE_SPRITE`：sprite 的逐通道饱和加法可用；
- `SPRITE_BATCH`：共享材质状态的批量 2D 实例；
- `TRIANGLE_BATCH`：共享材质状态的批量深度三角形。

Guest 只能在创建结果中存在相应能力时设置 `required_capabilities` 或 additive flag。Host 在执行任何命令前验证整份列表，避免半帧更新。

### 资源上传

资源通过 GameRender handle 的 `PXA_GAME_RENDER_IO_UPLOAD` 上传并绑定到 context 生命周期。首版固定 16 个 texture slot 和一个 256 色 RGB565 palette；`TEXTURE_SLOTS_48` capability 把 slot 上限扩到 `PXA_RASTER_MAX_TEXTURES`（48）。Voxel Craft 的材质由 `block_textures.c` 程序化生成，与旧 DDA 回退路径共用同一套 16x16 顶/侧/底贴图；INDEX8 路径启动时用中位切分把 45 个面片量化到 palette（保留 255 号白色给字体）。Host 支持 `TEXTURE_SLOTS_48` 时每方块上传顶/侧/底三张独立 16x16 贴图（42 张 + 字体占 1 个 slot），旧 Host 回退为每方块一张侧面贴图：

- `PALETTE_RGB565`：正好 256 个 little-endian canonical RGB565；
- `TEXTURE_INDEX8`：row-major INDEX8，宽高和 payload 长度必须完全一致；
- 第一帧提交前，同一 slot 的成功上传原子替换旧资源；失败时旧资源仍有效。首帧后资源被冻结，避免异步 raster 与资源释放竞态。

ESP backend 把资源、一个与逻辑输出同尺寸的 16-bit reciprocal-depth scratch buffer 和两个 48 KiB DrawList mailbox 放入 PSRAM。depth scratch 由串行 GameRender consumer 复用，不跟随颜色 buffer 重复分配。DrawList 执行期间不分配内存，关闭 context 时统一释放。

### DrawList

Header 包含 magic、ABI major/minor、总字节数、required capability、command count、frame ID 和 flags。每条记录都有 type 和 record size；首版 Host 对未知记录拒绝整帧，未来记录必须同时增加 capability 后才能协商使用。首版记录为：

- `CLEAR_RGB565`；
- `FLAT_QUAD`；
- `TEXTURED_QUAD`，顶点使用 12.4 屏幕坐标、12.4 UV、0..255 光照和 Q8 view depth；ABI 1.1 对 `u/z`、`v/z`、`1/z` 做透视校正。Host 在扫描线覆盖区间内以 8 像素小段计算端点透视坐标并做 Q8 增量，避免 ESP32-S3 上逐像素软件整数除法；同一记录也可通过 solid-color flag 表示带深度的纯色场景面；
- `AFFINE_UV` flag（capability `AFFINE_UV`）：Guest 只在 Host 声明该能力时设置。UV 改为屏幕线性插值、`1/z` 仍按透视插值写深度，省掉每块的透视除法和每像素的 `u/z`、`v/z` 累加。Guest 仅对深度跨度小、仿射误差低于约四分之一纹素的面启用，其余面仍走透视路径；
- `SPRITE`，仅在 capability 允许时接受 additive；
- `SPRITE_BATCH`，一个状态头后跟多个 16-byte 实例；
- `TRIANGLE_BATCH`，一个状态头后跟每三点成面的 12-byte 顶点。

校验包括 header/version/长度/record count、已知 flags、required capabilities、slot 是否已上传、顶点与 UV 的数值范围、退化多边形、目标尺寸、frame ID 单调性以及每帧命令/字节上限。协议错误返回 `PROTOCOL_ERROR`，超限返回 `LIMIT_EXCEEDED`，缺能力返回 `UNSUPPORTED`，无空闲 buffer 返回 `WOULD_BLOCK`。

## Voxel mesh

每个 16x24x16 chunk 保留 revision。mesh cache 只在 revision 或 chunk 坐标改变时重建。每个轴的每个切片先生成“当前 voxel 非空气且相邻 voxel 不遮挡”的二维 mask，再把相同 block/face 的连续矩形合并成一个 Quad。跨 chunk 边界通过世界读取判断暴露面。当前最小闭环把水、玻璃和叶片也按不透明块处理；透明材质分层尚未完成，不能把它计入首阶段验收。

每帧先对 chunk 包围盒做 fog-distance 和视锥粗裁剪，再对候选 Quad 做背面剔除。与 near/far/四个视锥侧面相交的 Quad 使用 Sutherland-Hodgman 裁剪；三角形和五边形以上的结果以退化 Quad/triangle fan 发送，不能因为单个顶点越过 near plane 而丢弃整个面。Host 使用 reciprocal depth 做逐像素遮挡；ABI 1.1 的不透明面按近到远提交以尽早拒绝被遮挡像素，无深度 fallback 才保留远到近顺序。High/Balanced/Performance 当前分别保留最多 620/480/320 个候选面；缓存或 DrawList 达到容量时按质量档位截断，并在 Guest stats 记录 clipped/dropped Quad。

支持 `PAINTER_DEPTH` 的 Host 改走扫描线透视贴图 + 逐像素 reciprocal-depth 测试：不透明面近到远、水面远到近且只读深度。该路径复用现有 16-bit depth scratch，避免覆盖位图对交叉面片的顺序依赖以及三角形光栅的逐像素透视除法；不支持新能力的 Host 仍使用原有回退。远景雾化不再大幅压低纹理亮度。模拟器截图只能校验画面，1x 的帧率必须以 ESP32 真机的 Host/Guest 遥测为准。

其余已落地的优化：候选排序改为按深度排序 16-bit 索引，避免在 PSRAM 里搬移整个 Quad 结构；投影后小于约 3x3 像素的贴图面降级成带深度的纯色面；Voxel 面光照本身是每面常量，Host 检测到三个顶点光照一致时跳过光照插值 setup 和逐像素光照累加，`light_rgb565` 改用精确的无除法 `x/255` 实现；Guest stats 新增 `affine_quads` 记录实际走仿射路径的面数。

## 分辨率与质量

GameRender context 的逻辑分辨率独立于 mesh 密度。High/Balanced/Performance 继续控制雾距离、可见 chunk 数、最大 Quad 和像素覆盖预算，Host 用 nearest-neighbor 放大。质量控制同时观察：

- Guest update、mesh rebuild、cull/project/sort 时间；
- DrawList bytes/command count；
- Host raster 时间和 covered pixels；
- Surface `WOULD_BLOCK`/queue wait、present 和 dropped frame。

只有 Guest 几何阶段超预算时才缩短视距或 Quad 预算；Host raster 超预算时先降低 pixel coverage/resolution；buffer wait 高时不继续降几何质量，而是报告显示消费瓶颈。升级和降级都使用连续窗口与迟滞。

## 内存预算

以 148x120、3 buffers 为默认：Host RGB565 buffers 约 104 KiB；reciprocal-depth scratch 约 35 KiB；256 色 palette 512 B；每方块顶/侧/底 16x16 INDEX8 贴图共 11.25 KiB；两个 48 KiB DrawList mailbox；Guest chunk mesh cache 使用固定上限。296x240 High 的三个 Host buffers 约 416 KiB，depth scratch 约 139 KiB，只在实时 telemetry 证明预算允许时使用。

旧 GuestMapped fallback 仍保留现有约 450 KiB 的 Guest 静态 framebuffer/depth/预计算表。完成 Raster 稳定性验证后可把这些 fallback buffer 放入单独构建 profile，但首个兼容版本不删除。

## 前台缓冲与截图

GameRender 只写 backend 选出的空闲 buffer。presenter 完成消费前该 buffer 不可重用；最新 pending buffer 也不是截图源。截图和像素采样必须从 presenter 已确认完成的 foreground/current buffer 获取。系统模态 UI 出现时 direct scanout 暂停，旧 foreground 可用于 LVGL 合成；模态消失后仍要求一帧新的完整输出才恢复 direct scanout。

## 验证与风险

Host 单元测试覆盖完整列表先校验后执行、裁剪、UV、RGB565、能力回退、buffer 替换/释放和 telemetry。Simulator 使用相同 ABI、kernel 和 ownership 语义，仅用于正确性；不能用其 FPS 推断 ESP32-S3。

主要风险是透明块仍暂按不透明处理（水面、树叶还没有真正的透明排序；水下用双面水面、缩短雾距、深水清屏色和全屏 additive 蓝色 tint 近似）、全屏 reciprocal-depth scratch 的 PSRAM 带宽、mesh rebuild 的瞬时峰值，以及生物/粒子目前使用低成本 billboard，细节不及旧像素路径的 box ray intersection。真机 profile 后再决定透明分层、实体贴图以及是否需要改成 tile depth。

性能数字必须标记来源。2026-09-16 在 ESP32-S3（pai-touch）实测：固定 4x 在连接 `pxadb logcat` 时为 14.2--14.6 fps，Host raster 约 23 ms，DrawList 约 19.5 KiB；固定 1x 在不连接日志时 HUD 约 8 fps。串口日志会与运行时 RPC 争用并显著压低 1x 可见帧率，因此不能把 logcat 期间的 FPS 当作实际交互帧率。

覆盖层现使用透视正确的纹理采样（深度变化小的面仍使用仿射），并让纯色实体参与同一遮挡掩码；连续空白覆盖字节最多按 24 像素合并贴图采样。覆盖模式先按近端深度排序，再对最近的至多 448 个候选面检查屏幕重叠及倒数深度的遮挡关系（包括完全重合的投影）；超过这个窗口的远处面仍是近端深度近似，遮挡关系成环时也只能回退深度键，不能等同于逐像素深度测试。方块选中框先裁剪到屏幕，再按体素可见性分段绘制，避免轮廓穿过前景方块。2026-09-23 在 esp32s31-korvo-1 的 800×480、1x 游戏场景中，连接 `pxadb logcat` 时观测到稳定窗口约 16–20 fps；更密集的场景及 pai-touch 仍需分别实测，不能由该数据保证全场景达标。
