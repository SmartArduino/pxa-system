# PXA 发行、接口与应用兼容策略

`VERSION` 是官方 PXA、C/C++ SDK、模拟器与 DevKit 组合发行的唯一版本源，采用 SemVer。首个候选发行是 `0.2.0-rc.1`，Git 标签为 `v0.2.0-rc.1`。CMake 的数值 VERSION 为 `0.2.0`；公共头文件、工具和发行清单保留完整候选后缀。发行校验拒绝头文件、IDF 组件元数据与 VERSION 不一致。

组合发行使用同一个编号，便于下载、复现与排错。应用和 Host 不要求编号相同：已编译应用通过协议兼容契约运行；源码项目通过 `pxa.lock` 选择准确 SDK 与工具链。板级固件、应用、WAMR 和上游 Clang 继续独立编号，并记录在发行/构建清单中。

| 契约 | 判定依据 | 改动规则 |
| --- | --- | --- |
| SDK 源码 API | SDK SemVer + 工具链锁 | 1.0 后破坏公共源接口增加 major；当前 0.x 明确记录不兼容变化 |
| Core Guest imports/envelope | Core major/minor（当前 1.0，20 字节 envelope） | 保持现有 import 和字段；破坏语义增加 major |
| 服务协议 | 服务 ID、major、最低/最高 minor、必须功能位 | 添加可选 opcode/字段增加 minor；破坏既有行为增加 major |
| Package/Container | 各自格式版本（当前 Manifest 0.7、Container 0.1） | 不由 SDK 或 Host 发行号推断格式 |
| Wasm/WASI | 内存模型、语言特性、实际 WASI 功能位 | 不支持特性或宿主能力时拒绝 |
| AOT | target、engine、完整 engine_abi、内存模型及特性 | 精确匹配；仅相同 WAMR 版本号不足以保证兼容 |
| Host 原生 C 链接 | `config/host-abi.json` 的 soversion（当前 0，保留已有 soname） | 与 SDK/Core major 分开管理；二进制破坏时提升 |

Host `--capabilities` 和真机 PXADB `runtime` 使用实际激活服务表，不把协议定义中的所有可选功能宣称为已实现。真机按需返回小记录，不保存额外能力缓存。发布前核对能力、实际启用的解释器、AOT 编译目标和 ABI。

SDK 新增高级模块不得提高所有应用的默认内存预算。已有模块的热路径、队列和 Guest 线性内存变化必须单独验证。版本与发行元数据不参与每帧处理，也不扩大深度/帧缓冲或命令队列。

每个项目锁定发行清单 SHA-256、工具链身份、基准协议和 O3 优化策略。清单包含源码提交、WAMR/LLVM 固定提交及文件库存。发行包不含私钥、源码构建缓存或用户数据。`sdk install` 保留旧 SDK；`sdk update` 不暗中抬高应用声明的最低服务版本。

发行流程：校验版本与协议 → 原生/SDK/打包回归 → 干净源码提交 → 基准 Linux 构建 → 收集动态库与许可证 → 可重定位离线 DevKit → 仓库外 C/C++ 构建、签名与兼容负向测试 → 模拟器截图与退出/重复启动 → 真机安装运行并核对资源 → 生成验收报告与 SHA-256 → Git 标签 → GitHub 草稿上传 → 下载复核 → 发布候选。

首次只发布实际验证的 Linux x86_64 二进制，尚未验证的 Windows、macOS 和 ARM 不列为支持。稳定 1.0 前应继续补充代表性旧二进制、旧 Host、操作系统和设备的兼容矩阵；候选版本不承诺未测试的任意历史组合。
