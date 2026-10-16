# AOT 分段加载与可选解释执行

PXA 的普通 AOT 从统一的可寻址读取器加载。Flash 文件系统、SD/FAT 和桌面文件
使用同一条路径，不要求文件支持内存映射。本阶段明确不支持 XIP。

## 加载与所有权

1. 打开一次文件，取得长度；读取头部及段目录，检查版本、长度和顺序。
2. TEXT 直接读入最终的可执行分配；Xtensa 经 D-Bus 地址写入，通过 I-Bus 执行。
   两个地址映射同一块物理 PSRAM。保留普通 AOT 的代码及重定位方式。
3. 其他标准段分别暂存，交由 WAMR 完成类型、导入、初始数据和重定位解析。
   初始数据与必要字符串由模块持有；返回时释放这些临时段。
4. 刷新代码缓存，关闭读取器，再创建 Guest 实例和线性内存。

单次读取最多请求 16 KiB，直接写入目标地址，没有额外的 16 KiB 中转缓存。
短读会继续补齐；提前 EOF、读取错误和越界长度会终止加载。读取器在加载完成后
关闭，不进入逐帧执行路径。分段加载主要降低启动峰值，稳态代码区仍完整驻留。

WAMR 自定义段可能借用输入数据。目前含自定义段的 AOT 安全回退到原有整文件
加载，并按运行时所有权保留源缓冲。这不是所有 AOT 都能固定内存加载的承诺。
XIP 在执行前拒绝，不自动复制到 Flash，也不改变文件系统或分区布局。

`tools/wamr/patches/0001-owned-aot-sections.patch` 修正预加载段的所有权契约：
解析 INIT_DATA 前开启复制，TEXT 在成功和失败时都由 WAMR 接管，部分加载失败
按原始分配基址释放，设置当前 AOT 格式版本，并在成功后刷新指令缓存。补丁只应用至构建目录，WAMR
子模块保持干净；桌面和 ESP-IDF 使用相同补丁。普通 AOT 文件格式与 Guest ABI 不变。

## Host 接入

```c
pxa_wamr_engine_config_t config = {0};
config.struct_size = sizeof(config);
config.open_artifact = pxa_wamr_open_artifact_file;
// 配置组件数、Guest 栈、运行时及 Artifact 分配器等现有选项。
```

内置 POSIX/VFS 实现打开文件一次，不使用 stdio 缓存。产品只需将包根目录交给
现有激活流程，例如 Flash 挂载目录或 SD 挂载目录；加载器不硬编码它们。
这项改动不自动实现 SD 挂载、应用商店或安装目录迁移。

读取器本身不取消旧的静态工作区预留。需要降低 RAM 的嵌入方还应提供现有的
按需运行时、线性内存与 Artifact 分配器；桌面与 ESP 产品 Host 已这样配置。
只设置 `open_artifact` 而继续使用默认静态工作区，不会自动省掉工作区的预留容量。

自定义后端提供 `pxa_wamr_artifact_reader_t` 的长度、`read_at` 和 `close`。
`read_at` 可以短读，成功但返回 0 表示 EOF；未满足请求的 EOF 会被视为 I/O 错误。
同一读取器由加载线程独占。后端成功打开后必须提供完整回调；失败打开由后端
自行清理。`read_artifact` 保留供已有嵌入方使用，配置 `open_artifact` 时优先采用它。

包仍须经过原有签名、清单与内容验证；分段读取不替代安装验证。底层存储必须
保证运行时读取的已安装包不会被外部并发改写。正在加载时不支持拔除 SD 后继续启动。
成功加载、文件关闭后，普通 AOT 执行不再读取其代码文件；资源、存档等仍可能访问 SD。

## 可选解释执行

解释执行是显式的构建/包选择，不会因 AOT 内存不足而自动切换：

```sh
# 工作区开发入口；编译 C++ 为可移植 Wasm 并签名打包。
bash tools/app.sh build <app-id> --target wasm --output <目录>
```

也可以在 `package.json` 中将指定组件设为 `"artifact": "wasm"`；其余组件仍使用
AOT。固件必须启用 `CONFIG_WAMR_ENABLE_INTERP`。pai-touch 等现有板配置已启用。

解释器仍需要字节码、执行结构和 Guest 线性内存；当前 Wasm 路径保留源文件缓冲，
不把 Wasm 体积当作总 RAM 用量。游戏优先保持 AOT，工具类应用可比较两种模式的
实际交互延迟、启动峰值与稳态占用。AOT-only 包中没有 Wasm 时不能直接切换模式。

## 验证

`pxa_wamr_aot_reader_test` 覆盖源文件覆写后运行、短读、逐次 I/O 失败、所有长度
截断、加载阶段逐次分配失败、接管代码后的解析失败、XIP 拒绝及 2 MiB TEXT。
Linux 额外跟踪可执行映射的回收，并比较同一模块的分段/整文件加载峰值与稳态。
这些计数是请求的分配字节，不是进程 RSS 或真机物理堆。

`pxa_wamr_reader_engine_test` 在实际 Component 激活流程运行短读文件后端，覆盖
Wasm、标准 AOT、自定义段回退、事件、停止和错误清理，并核对打开/关闭次数。
原有整文件嵌入路径仍由 `pxa_wamr_engine_test` 回归。

本轮实际设备与模拟器记录见工作区 `docs/benchmarks/aot-stream-20261015.zh-CN.md`。
