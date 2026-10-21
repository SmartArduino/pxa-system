# GameRender 投影预计算

GameRender 的 projected spans 和 frame spans 可以在完整验证过程中保存投影结果，
供执行阶段复用。Guest 的包格式、服务特征和同步调用约定不变；这是 Host C
实现选项，不需要增加 Guest opcode 或让应用管理 Host 临时内存。

## 配置与生命周期

`pxa_game_render_config_t` 的两个可选字段控制此功能：

```c
config.span_prepare_allocator = raster_allocator;
config.span_prepare_bytes = 96u * 1024u;
```

ESP 的板级入口为 `CONFIG_PXA_GAME_RENDER_PREPARE_KIB`，默认 0；设置为 96
可启用上述容量。仅在对应负载的实测整帧收益明确后开启。

分配器使用现有 `pxa_memory_allocator_t`，因此分配、释放和失败均计入统一资源预算。
分配器及 owner 必须比所有使用它的 context 活得更久。ESP 适配器选择 external
memory 的 raster 类别，禁止为了缓存回退到 SRAM。

每个计算 context 在第一次 projected/frame 调用时尝试分配一次，后续调用复用；
关闭 context 时释放。分配失败不在每帧重试，也不拒绝有效绘制。将分配器设为
NULL 或容量设为 0 可关闭预计算。容量上限为 1 MiB，按应用和板型的预算选取，
不能把示例容量视为所有场景都能命中的保证。

直接使用 raster C API 时，调用 `pxa_raster_projected_spans_prepare()` 或
`pxa_raster_frame_spans_prepare()`，传入 Host 自有工作区。实现自行在工作区内
保留对齐填充，不要求 capability allocator 提供较强的对齐。工作区必须与借用的
命令、纹理、光照、颜色及深度内存独立，且在所有执行范围返回之前保持有效。

## 验证、容量不足与双核执行

预计算不写颜色或深度缓冲；所有命令、光照、别名关系和工作量上限验证成功后，
才发布 `pxa_raster_spans_t.prepared`。任一命令不合法仍整批拒绝，不留下部分画面。
工作区不足时丢弃整份计划，继续验证，随后使用原投影路径。调用者可以通过
`prepared != NULL` 检查是否真正命中；不能仅凭成功分配就推断快速路径有效。

计划保存每行的位置及深度起点，每 32 像素保存原采样器使用的四个 16 位参数。
投影计算保留原来的 binary32 运算和截断顺序；执行时复用已有光照内核。
它不保存完整纹理副本，不新增帧缓冲，也不跨帧保留 Guest 指针。

`pxa_raster_spans_split_hint(batch, primary_share)` 根据 32 个空间分段中的像素
及跨度工作量估算分界，份额取 1–255（分母为 256）。后端仍须将分界对齐到适合
缓存一致性的地址，并保证两个执行范围不相交。未命中预计算时返回几何分界。
同一范围内的材质、颜色和深度写入顺序不变；所有工作必须在同步调用返回前完成。

ESP 后端复用现有 worker，根据两核上一批完成耗时平滑调整份额；worker 未实际
领取的批次不参与反馈。没有新增线程、任务栈或异步 Guest 内存访问。

## 回归边界

`game_render_spans_test` 检查 0–4096 字节容量、16 种起始偏移、前后保护字节、
反向范围执行、重叠写入、非法末条命令、资源预算释放以及分配失败回退。
设置 `PXA_TEST_PREPARE_FAIL=1` 可运行同样的输出检查并验证只尝试分配一次。
Apps 仓库的 `run_quake_projected_spans.py` 直接提取当前 Quake 原实现，执行
40,000 组投影、光照和深度逐字节对照，覆盖普通、预计算及容量耗尽路径。

这些检查证明输出和边界约束，整帧收益仍须由固定场景的实机对照确认。
