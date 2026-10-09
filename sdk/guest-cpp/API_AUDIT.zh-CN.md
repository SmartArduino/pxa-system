# C++ SDK 应用接口审计（2026-10-09）

应用调用已有服务应使用类型化接口，不应填写 service/opcode、TLV tag 或
PXA 消息头。SDK 和生成器内部使用 `wire` 是实现细节，不需要整体改名。
本轮发现的主要问题是应用数据编解码缺少方便的入口，以及通用文本类型被
放进协议命名空间。已补齐以下能力；Host ABI、存档格式和默认容量不变。

| 原因/缺口 | 完成的修改与收益 | 内存变化 | 验证 |
| --- | --- | --- | --- |
| 保存一个整数也要手写字节编解码 | `StorageService::get_value<T>` / `set_value`，精确校验类型长度、规范布尔值，创建任务时拥有键和值 | 不新增服务字段；四字节值的最终封包最多 96 B；一项服务协程，无通用 set 的 512 B 临时封包和第二个服务协程 | 旧协议黄金字节、临时键和值生命周期、错误结果及容量边界；模拟器保存并重启读回 1 |
| Surface 示例按字节写 RGB565 | `SurfaceFrame::rgb565()` 和可选 `Rgb565Pixels`；处理实际 stride、padding、未对齐地址与小端格式 | 不新增帧缓冲或租约字段；仅借用视图 | 行边界、未对齐和 padding 测试；真实 Surface 模拟器截图 |
| 服务结果与生成 IPC 结构暴露 `wire::OwnedText` | 公开 `FixedText<N>`，同步 Device、Sensor 和 IPC 生成器；旧名为同类型别名 | 原布局和容量不变，无堆分配 | 类型相等检查、UTF-8/容量失败回滚、生成物检查、真实 IPC 请求 |
| 文件、Work、游戏存档借用 Host 的低层字节函数 | 可选 `binary::Reader/Writer`、`encode/decode` 与带偏移的 `read/write`；字段必须明确选择整数/IEEE 浮点/布尔值，拒绝隐式结构体序列化 | 使用调用方存储，不预留容器或全局缓冲 | 小端黄金字节、浮点位表示、越界不推进位置、截断和多余字节；ASan/UBSan |
| voxel 的分析暂存数据像协议包一样手写偏移 | 改为 12 B 本地 `PreparedHeader`，使用 memcpy 避免未对齐及别名问题 | 仍为 12 B，复用原暂存空间，仅 VOXEL_PROFILE 使用 | 分析模式 ASan/UBSan，暂存与直接绘制命令一致性测试 |

普通示例 `.cpp` 和 voxel-craft-cpp 非测试源文件已不需要直接调用
`pxa::wire`。生成的协议编码函数以及检查 ABI 的测试仍使用底层接口。

```cpp
auto count = co_await ctx.storage().get_value<std::int32_t>("count");
auto saved = co_await ctx.storage().set_value("count", std::int32_t{42});

auto pixels = frame.rgb565();
if (pixels) {
    auto row = pixels->row(y);
    if (row && x < row->size()) (*row)[x] = 0x07e0;
}
```

FixedText 保持原有非空 UTF-8 约定。RGB565 视图不延长租约，present、关闭或
销毁帧后必须丢弃；行的 x 索引采用 span 的调用方保证边界约定。旧的 Storage
字节接口仍借用 key/value/output/外部 packet；新类型化入口创建任务时拥有
键和值。自定义数据的版本、校验和和业务合法性仍由应用决定。

验证结果：完整 C++ Guest host 套件通过；binary、storage_value、surface
定向 ASan/UBSan 通过；voxel 的 VOXEL_PROFILE=1 ASan/UBSan 全部通过；
256 组存档状态的 20480 字节与修改前工作区完全一致；模拟器 13/13 通过。
storage、files、work、surface、ipc-stats、voxel-craft-cpp 均生成 Wasm、
Linux x86_64、ESP32-S3 和 ESP32-S31 AOT，五个 SDK 示例在产品模拟器启动、
截图并正常退出，Storage/文件重启读回及 IPC 交互另行验证。
发行脚本生成的独立 SDK 包使用包内源码、头文件、生成器、CMake 和打包工具
再次构建 Storage 的 Wasm/x86 AOT 成功，未依赖原 SDK 的头文件或实现源码。

类型化存取测试的池预留为 8208 B，峰值为应用任务和服务任务两个槽，完成后
归零。该结果证明本次接口没有扩大默认任务池，不代表设备 SRAM/PSRAM 的
实测峰值；本轮没有安装真机包、没有测新的设备 FPS，也不宣称渲染提速。
本地日志、包与截图位于工作区 `local/sdk-api-audit-20261009/`。

使用相同当前 SDK/工具链/编译参数，原示例源码与迁移后的示例另行对照：

| 示例 | Wasm 字节：原版→迁移版 | x86_64 AOT 文件字节：原版→迁移版 | Wasm 初始线性内存 |
| --- | --- | --- | --- |
| storage | 71460→70590 | 112532→113044 | 均为 131072 B |
| surface | 56924→56891 | 97148→97364 | 均为 131072 B |

Surface 的线性内存上限也保持 2 MiB。这是接口迁移对照，不是完整 SDK 历史
版本或 C/C++ 性能对照。AOT 文件增加 512/216 B，不能据此宣称代码映射内存
完全相同；本轮未测设备代码映射和真实堆峰值。新增能力不为未使用它们的应用
预留缓冲、池或常驻字段，借助节裁剪按需链接。详细数据见本地
`example-comparison.json`。

后续应继续完善：

- 一些异步通知仍由应用通过 `Event.service/opcode` 手动分流；可进一步提供
  类型化匹配/订阅，保留现在的有界事件路由与取消语义。
- 通用 Storage 字节接口与部分服务的字符串、span 借用规则不一致；可以
  继续统一创建任务时编码的行为，同时验证协程帧和池峰值。
- 类型化 Storage 本轮只覆盖标量，自定义结构仍需 Reader/Writer 或自己的
  codec。应通过显式 schema/codec 扩展，不能直接写原生结构体布局。
- 文件接口仍明确返回短读、短写和 would_block；尚未加入持有部分进度的
  类型化流读写。不能用表面方便的 read<T>() 隐藏这些状态。

这些是剩余设计项，不是本轮已实现的能力。
