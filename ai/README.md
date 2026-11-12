# PXA Agent Kit 0.7.1

0.7.1 新增带签名资源包的游戏验收：`verify APP --resource-package local/artifacts/game/resources.pxa --store-screenshots`（MCP：`pxa_verify(resource_package=...)`）。工具检查资源代次与应用绑定、签名身份及文件摘要，将素材接入隔离模拟器的只读媒体目录。再用场景输入进入关卡并检查原始截图；运行成功本身不代表游戏可玩。当前要求代码与资源使用同一签名密钥。

配合 DevKit 与固件 0.11.0；Browser Host 独立编号。联机指引根据实际能力区分浏览器
原生 WebSocket、桌面/固件 TCP 客户端、监听、UDP 与 DNS-SD；补充权限生命周期、
跨公网条件、真实消息验收以及真机锁屏对点按测试的影响。屏幕旋转沿用 Window 0.5。

[English](README.en.md) · [一句话上手](https://pxa.doit.am/docs/ai-development/) · [AI 入口](https://pxa.doit.am/ai.md)

把这句话交给 Codex、Claude Code、DSH 或能读网页并执行命令的编程助手：

> 请读取 https://pxa.doit.am/ai.md，在当前目录为我配置 PXA 开发环境，运行模拟器检查环境，然后等待我的应用需求。

工具在电脑上运行。已有 AI 账号/客户端可直接使用，无需另配模型 API Key、ESP-IDF、CMake 或 LLVM。SDK、工具链、模拟器按官方签名与 SHA-256 校验。首次下载后可离线构建。Linux x86_64/glibc 2.39+ 和 Windows x64 可使用原生包；Windows 包目前以 Wine 验证，首次运行按提示安装微软 VC++ x64 运行库。macOS、较旧 Linux 使用 Docker Desktop 或 Podman 的 Linux amd64 容器。首次安装容器引擎可能需要操作系统权限或启用虚拟化，登录 AI 和浏览器商店账号仍由用户完成。

## 命令入口

安装 [uv](https://docs.astral.sh/uv/getting-started/installation/) 后，一条命令准备独立工作目录：

```sh
uv run --python 3.12 https://pxa.doit.am/setup.py --workspace ./my-pxa
```

或使用源码/已经校验并解压的发行包：

```sh
uv run --python 3.12 --locked --project ./ai ./ai/agent.py --workspace ./my-pxa setup
```

`setup` 自动选择官方 DevKit 0.11.0 或签名容器镜像，安装项目内的 skills、平台 MCP 配置、CLI 启动器，最后编译并运行真实 Guest、保存检查截图。失败时返回失败，不会把下载成功当成验收。`--sdk /path/to/devkit` 仅复用用户自己选择的完整 SDK，不宣称它经过下载签名校验。离线分发可以用 `setup.py --channel /path/to/ai-kit-0.7.1.json --manifest /path/to/distribution.json`，归档和签名放在各自清单旁。

在 `my-pxa` 目录中使用以下命令（Windows 使用 `.pxa\pxa.cmd`）：

```sh
.pxa/pxa doctor
.pxa/pxa init apps/counter --id pxa-counter
.pxa/pxa run apps/counter --profile pai-touch --watch
.pxa/pxa verify apps/counter
```

先停止 watch 再运行 verify；run 已包含构建，不必为每次保存重复执行 build。`run` 启动实际桌面 Guest；容器在后台运行，用 MCP 截图与输入工具交互。`verify` 默认覆盖五种屏幕，返回日志、截图与 `result.json` 的路径。检查截图后再判定 UI 正确。测试数据写入工作区被忽略的 `local/artifacts/`，不会进入应用资源包。应用保存在 `apps/<name>`。

## AI 平台

| 平台 | 自动安装到项目内 | 使用方式 |
| --- | --- | --- |
| Codex | `.agents/skills`、`.codex/agents`、`.codex/config.toml` 的 PXA MCP 项 | 打开工作区，使用技能、MCP 或 CLI |
| Claude Code | `.claude/skills`、`.claude/agents`、`.mcp.json` 的 PXA MCP 项 | 按客户端提示信任项目 MCP 后使用 |
| DSH | 兼容的 `.agents/skills`、CLI；`.pxa/dsh-mcp.yaml` 提供官方 Cordis MCP 配置片段 | CLI 可直接用；MCP 按本地 DSH composition 合并片段 |
| 其他编程助手 | `AGENTS.md`、CLI、标准 stdio MCP | 支持 Agent Skills/MCP 时加载；否则直接执行 CLI |

重复 setup 只更新本工具拥有且未被用户修改的配置块/文件。保留其他 MCP、项目规则及全局权限/模型设置。若客户端未发现新技能或 MCP，重新打开项目。自定义开发/验收 agent 是可选职责，不自动开启委派，也不改变平台权限。

## 实现与调试闭环

> 做一个番茄钟，支持开始、暂停、重置，适配圆屏和横屏。请实现、编译、运行模拟器，实际点击并查看截图，发现问题继续修复。

AI 先读 `pxa-app-dev`、`pxa-debug`，按现有 SDK 头文件实现；构建后用真实 Guest、截图、触摸或控制器输入验证。反馈时描述预期，例如“按钮再大一些，手柄 B 返回后保留倒计时”。继续修改同一个应用，保留原 app ID、签名身份、SDK 锁及存档。

默认界面围绕实际任务组织，省去未经要求的标语、装饰性副标题和重复说明；保留必要反馈、清晰标签、无障碍及用户指定的剧情和风格。操作教学按需出现，完成或关闭后保存状态，并能从帮助重新查看。相关规则见 [界面内容](skills/pxa-app-dev/references/interface-content.md)，摘要随 `init` / `new` / `context` 返回，也接入项目指引和 MCP。

制作游戏时先运行完整的核心玩法，再扩展到请求范围；尽早根据实际 SDK 选择渲染、字模和音频路径，复用增量构建，并验证冷启动文字、拖动缩放、繁忙玩法和帧节奏。具体取舍见 [游戏开发](skills/pxa-app-dev/references/game-development.md)。`verify --scenario` 支持 `relaunch`：在同一隔离测试存档中重新打开已验证应用，用于检查教学状态和游戏进度；不清除日常存档。等待实际保存成功后再重开，并检查重开后的状态及正常使用画面。

选用 DevKit 0.11.0+ 时，新的计时器或计数器可直接从可运行配方开始：

```sh
.pxa/pxa recipes
.pxa/pxa new apps/tomato --recipe timer --title 番茄钟 --minutes 25 --break-minutes 5
.pxa/pxa verify apps/tomato --scenario apps/tomato/tests/journey.json --require-ui
.pxa/pxa new apps/counter --recipe counter --title 计数器
```

MCP 对应 `pxa_recipes`、`pxa_new`。配方带动态安全区、触摸/手柄、异步检查点存档和
真实节点验收，不覆盖已有目录、不自动迁移 SDK。计时器支持开始、暂停、重置和手动
专注/短休切换，使用单调时间；重新打开按最后检查点恢复为暂停，并非关机后运行的闹钟。
完成态可用 `--define PXA_RECIPE_TIMER_SECONDS=6 --scenario apps/tomato/tests/complete.json` 验收；
测试键与正常存档分开，交付前重新构建不带宏的包。其他需求可从当前 SDK 的虚拟列表、
设备/传感器、game-input 示例开始，再让 AI 按需求完善，不把骨架生成为成功验收。

一组已知的连续操作可用 MCP `pxa_interact(job_id, steps)`：复用同一个模拟器调试连接，依次点击、滑动、按手柄、等待或截图。最多 32 步、8 张截图、累计等待/按住 15 秒；完整检查通过后才操作。返回批次 job_id 和证据路径，等任务结束再用 `pxa_view_image` 检查原图。需要观察中间结果时拆成短批次，同一模拟器不并发输入。CLI 可用 `verify --scenario`。

批次结束后，`pxa_job_status` 直接附带 `batch` 结果。停止批次用 `pxa_job_stop`：等待执行进程取消并松键，正常取消后模拟器可继续使用；执行进程无响应时才停止它所属的模拟器。取消不算验收通过，继续前先检查当前画面，不重放整批操作。

MCP 长操作返回 job_id，通过 `pxa_job_status` 获取结果，`pxa_screenshot`/`pxa_view_image` 返回可供 AI 直接查看的图片。关闭 server 时清理它自己启动的进程/容器。CLI 是同一实现的后备入口，不依赖 MCP 是否启用。

## 真机与官方固件

```sh
.pxa/pxa doctor
.pxa/pxa device info --port /dev/ttyACM0 --baud 2000000
.pxa/pxa install apps/counter --port /dev/ttyACM0
.pxa/pxa firmware status --port /dev/ttyACM0
.pxa/pxa firmware prepare --board doit-gamekit-1
.pxa/pxa firmware upgrade --board doit-gamekit-1 --port /dev/ttyACM0
```

先发现串口并核对 INFO 的板型、CPU，再构建匹配的 S3/S31/P4 AOT；安装保留已有应用数据。截图、按键、内存使用 host 端 PXADB，无需给容器转发 USB。控制器注入在操作后自动 `INPUT CANCEL`，模拟按键不声称为机械按压。

设备无响应时先确认正常开机、USB 数据线、正确端口和 PXADB 开关。参考系统界面中，
进入「设置 → 设备信息」，快速连续点击「系统版本」7 次，再下滑到「开发者」打开
「PXADB 调试」；不同产品界面可能不同。BOOT/RESET 是固件下载操作，不能代替打开
运行中的 PXADB。ESPP4-A 的 UART-DEBUG Type-C 使用 UART0，默认 2000000 波特率。

`device-profile --port PORT --package apps/counter/dist/pxa-counter.pxa` 或 MCP
`pxa_check_compatibility(package=..., port=...)` 在上传前比较实际包的 Core、服务/功能位、执行模式、
CPU/AOT ABI 和容器要求。SDK 的 `device compatibility PACKAGE --port PORT` 使用同一检查。
固件与 SDK 发行号不同不等于不兼容；已知不兼容会阻止上传，旧固件缺失字段标为 `unknown` 并提示。
需要未知项也阻止上传时，使用 AI Kit 的 `install --strict-compatibility`
或 MCP `pxa_install(strict_compatibility=true)`；直接运行 SDK 时，先确认其 `run --help`
支持 `run --device PORT --strict-compatibility`。安装成功后仍检查实际激活状态。

AI 安装在完成构建与签名检查后，保持同一条主机串口连接完成设备复核、兼容性检查、
停止目标、更新与激活确认，避免 UART 桥接设备在反复打开串口时复位。编译期间若
设备的 CPU、板型或可识别序列号变化，会在写入前停止。启动排队不等于已运行。

`firmware status` / MCP `pxa_firmware_status` 只读 INFO 和已验签的发行清单，
不下载镜像、不复位或刷写；未读真实分区表前，app-only 是否匹配保持未知。
板型名称和发行号也不证明当前镜像来自官方。无法识别时先确认硬件，选择支持板的网页烧录或板级适配流程。

设备客户端随 AI Kit 更新，不要求为使用新设备命令修改项目 SDK。编译器、头文件和模拟器按
项目锁定的 SDK 使用；直接运行 DevKit CLI 时，以它的 `--help` 支持的参数为准。

固件默认 app-only，校验官方签名、镜像摘要与设备实际分区表后才写入，不碰数据分区。分区变化会停止，例如旧版 Mosaico 升级到 0.5.0。备份并获得用户明确清数据授权后，才使用 `--mode full --erase-data`。S31 可能需要按住 BOOT、按 RESET、松 BOOT 进入 ROM，写完按 RESET；端口可能变化。写入完成和重启后 INFO 验收分别报告。

## 显式迁移 SDK

先停止开发中的 watch、构建和模拟器。工作区选择与项目锁各自保留：

```sh
.pxa/pxa workspace migrate --sdk-version 0.9.0
.pxa/pxa context apps/counter --query Button
# 仅在也要迁移该应用时，再显式更新它的锁：
.pxa/pxa sdk update apps/counter --offline
.pxa/pxa verify apps/counter --profiles pai-touch espp4-a
```

将示例版本替换为已选定的目标发行版。`--sdk PATH` 可选择现有 DevKit，`--manifest PATH`
可指定离线签名清单。迁移保留应用源码、`pxa.lock`、签名身份和存档，归档旧审计并继续保护新 SDK；
下载或验证失败时保留原配置。已有项目仍使用自己的锁定版本，工具会查找独立 DevKit 与 Agent
缓存中的版本；缺失时明确提示安装。`sdk update APP` 才会改变该项目的锁，之后须重新构建、
运行和验收，不能靠修改版本号伪造兼容。

MCP 对应 `pxa_workspace_migrate(sdk_version=...)`，它会先停止本服务拥有的会话，完成后轮询
`pxa_job_status` 会刷新服务环境。从 CLI 迁移后，请重启已经连接的 MCP 服务以读取新配置。
普通重复 setup 会保留工作区已有 SDK 选择。

## 安装体积、下载缓存与修复

原生发行包默认提供 PXA 使用的 `wasm32-wasip1` 工具链，保留 C/C++ 编译器、
链接器、归档与二进制检查工具，不附带其他 WASI 目标和独立 LLDB/Clang 工具。
裁剪仅发生在发行包组装阶段，已安装的 SDK 内容不随 AI Kit 更新而变化；
所有必要许可证继续随包提供。维护者可用 `--toolchain-profile full` 组装完整工具链。

成功安装原生 SDK 后，默认释放它的下载归档，保留已验签的清单和签名。
需要离线复装时，在 `setup.py` 或 Agent Kit `setup` 后加 `--keep-archives`，
也可保留完整的离线发行目录。清理下载缓存与删除 SDK 版本是不同的操作：

```sh
.pxa/pxa doctor --usage
.pxa/pxa sdk cache
.pxa/pxa sdk prune --dry-run
.pxa/pxa sdk prune
.pxa/pxa sdk repair --keep-archives
# 已保留归档时，可离线按已安装的签名清单修复：
.pxa/pxa sdk repair --offline --keep-archives
```

`prune` 只删除已安装 SDK 的有效签名清单能识别的归档，等待正在解包的读取者；
保留安装目录、其他用途的下载、项目锁、私钥及存档。`repair` 显式重新验签、下载/复用
并替换相同版本，失败时保留原安装；操作前停止正在使用该 SDK 的构建、watch 和模拟器。
离线归档不足时提供 `--manifest /media/release/distribution.json` 及同目录完整组件。
工作区自选 SDK 路径不会被自动替换，`--sdk-version` 可明确指定要修复的托管版本，
并不切换工作区或项目锁。MCP 对应 `pxa_doctor(include_usage=true)`、
`pxa_sdk_cache` 和 `pxa_sdk_repair`；MCP 清理默认只预览，实际删除需 `dry_run=false`。

支持 `clean` 的 DevKit 还可回收项目中的旧构建目录和旧包缓存：

```sh
.pxa/pxa clean apps/counter --keep 2 --dry-run
.pxa/pxa clean apps/counter --keep 2
# 需要连可重新生成的 AOT 缓存也释放时，再加 --aot：
.pxa/pxa clean apps/counter --keep 0 --aot
```

清理与构建共用锁；保留已发布包、当前构建记录及输出链接引用的配置/代次、源码、
签名身份和模拟器存档，不追随缓存目录链接。`--keep` 指定最近缓存的保留数量，
`0` 也不会删除被当前输出引用的内容。MCP 使用 `pxa_clean`，默认只预览。
旧的锁定 DevKit 不支持该命令时，先按显式迁移流程选择支持版本；不要修改安装目录。

## 上架 app.doit.am

生成商城简介时，用一句话说明实际用途或核心玩法，通常 20–60 个中文字，最多 200 字符。应用写清用户能做什么，游戏写清主要操作与目标；详情只补充已实现的特色和必要限制。不写宣传口号、空泛赞美、AI/PXA 制作说明或按键教程，不虚构离线、存档、联机、兼容性和性能。将文案写入 `store.json`，运行 `store prepare` 查看 `listing_guidance` 与 `listing_text.needs_text`；缺失时先检查实际产品再补写，工具不会生成通用兜底简介或静默截断超长文本。 完整规则见 pxa-release 的 `references/store-listing.md`。

```sh
.pxa/pxa store login --wait
.pxa/pxa store build apps/counter --targets esp32s3 esp32s31 esp32p4
.pxa/pxa store publish-targets apps/counter MANIFEST --changelog '首次发布'
```

以上账号记住与分目标流程需要 Agent Kit 0.7.0。将 `MANIFEST` 替换为构建结果返回的 `manifest` 路径；产物保存在工作区被忽略的 `local/artifacts/pxa-store-build/<run>/`，避免多份输出互相影响 SDK 源码输入校验。每个目标一个 `.pxa`，共享编译和资源缓存；S31、P4 即便同为 RISC-V 也独立打包。只选择实际交付的原生目标；工具始终自动补上独立 Wasm `.pxa`，即使只面向设备也必须上传，模拟器目标通常用于验证。所有变体保持同一 app ID、签名身份、版本与发布序号，上传后归入同一待审核版本。整组文件和构建输入先校验，再上传资料与素材一次；部分失败会返回已完成和剩余目标，先检查商城状态，再仅补交未完成目标。

整组校验后先上传 Wasm，再上传原生包。缺少独立 Wasm 版本会阻止提交；补交单个原生目标时，工具会先确认商城已有同版本、同发布序号、同签名者的独立 Wasm 包：

```sh
.pxa/pxa store publish apps/counter apps/counter/dist/pxa-counter.pxa --changelog '首次发布'
```

首次在浏览器登录并选择“授权并记住这台电脑”，之后新应用和更新都复用该账号。工具自动证明本机持有应用签名私钥，登记公钥、创建/复用作品资料，并获取 30 分钟有效的单应用上传权限；过期后自动重新获取，正常上架不再逐个应用点击授权。电脑发布凭证在活跃使用时自动续期，连续 365 天未使用需重新登录。私钥不上传，凭证不进入聊天、命令参数或源码。`store.json` 可设置 name/summary/description/kind/category/tags；未提供的既有字段保留，显式名称、简介、详情、分类和标签更新遵循商城资料审核，未指定字段保留。已上架应用的资料和图片变更形成私有草稿；待审或驳回期间，旧资料、旧图片和已发布版本继续公开，通过后才切换。返回的 `listing_update` / `media_updates` 包含审核状态及 `public_listing_retained`；不要通过下架、重建应用或重复上传来恢复待审资料。

`store account` 查看当前账号，`store account --refresh` 检查并续期，`store logout` 撤销此电脑并清除本地商城凭据，`store login --renew --wait` 退出后切换账号。商城“发布授权”页也可撤销电脑，立即使其已获取的上传权限失效。支持的 Python `keyring` 系统后端可用时优先存入系统凭据库，否则使用源码目录外、目录权限 0700 / 文件权限 0600 的私有存储；Windows 使用用户应用数据目录。密钥库暂不可用时保留原登录信息，恢复后重试。账号凭证只用于发布授权，不提供账号管理或管理员权限。

`store authorize <project> <package>` 仍可使用：已有账号时自动授权应用，首次也可直接确认并记住电脑；`--app-only` 保留旧版单应用、30 天的授权方式。低层 SDK `pxa publish --token-file` 仍只接受单应用上传凭据。

默认拒绝将多个 AOT 目标合在一个商城包中；明确需要通用包时可用 `store publish --allow-universal`。普通 `build --target a,b` / MCP `pxa_build(targets=[...])` 仍是合包构建，上架优先使用 `store build`。

先运行 `.pxa/pxa store prepare apps/counter` 读取商城当前图片规格、分类与标签规则，并预检本地素材。推荐 512×512 图标和 1280×960（4:3）封面。可使用图片工具生成图标和封面；没有图片工具时，由用户提供图片或明确选择跳过上传。

在项目内 `store.json` 设置 `kind`、`category` 和 `tags`，例如：

```json
{
  "kind": "app",
  "category": "tools",
  "tags": ["计数", "工具"],
  "icon": "store/icon.png",
  "cover": "store/cover.png"
}
```

路径相对于应用项目。标签最多十个，每个最多 24 个字符。`icon` / `cover` 设置为 `null` 表示明确跳过该项上传；也可传 `--skip-icon` / `--skip-cover`。跳过保留商城现有图片，不会删除。没有素材且尚未选择跳过时，预检返回 `needs_choice`，发布前应完成选择。`store publish --icon icon.png --cover cover.png --screenshot screen.png` 可同时上传素材，命令行路径相对于工作区。分类与标签按 `store.json` 的显式字段更新，未提供的字段保留原值，空标签数组清空标签。资料改动沿用商城审核流程。最终确认本地包摘要与服务器接收摘要一致，状态为 pending_review；审核通过后才公开。准备多目标设备包时先按实际受支持目标构建，再上传，不用桌面包冒充真机包。

## 维护与兼容

AI Kit 独立版本，不修改已发布 DevKit。`setup.py` 内置发行公钥，先验证签名清单，再校验归档及逐文件摘要。`uv.lock` 固定 Python 依赖。源码中的 `install.py` 仍支持仅安装 skills：`install --project <dir> --platform codex --agents`，不需要第三方 Python 库；移除这些文件用 `remove --project <dir>`，遇到本地修改会保留。项目 MCP、规则和启动器由 setup 另行管理，不属于旧安装器的移除范围。

模拟器结果不代表硬件 FPS、音频延迟或所有操作系统已实测。反馈问题时提供工具版本、操作系统、板型、复现步骤和去除秘密的日志。

## 任务边界与更短的开发循环

Agent Kit 0.3.3 可直接创建 C 或 C++ 项目。SDK、ABI、运行时与板子代码是应用任务的只读输入：

```sh
.pxa/pxa init apps/c-demo --id pxa-c-demo --language c
.pxa/pxa context apps/c-demo --query ui --language c
.pxa/pxa build apps/c-demo
.pxa/pxa verify apps/c-demo --profiles pai-touch
.pxa/pxa scope check
```

新建应用自动开始边界记录；修改已有应用前运行 `scope begin --mode app --project apps/name`。
`context` 返回当前版本的头文件行号、片段与示例，避免重复读取全套文档。开发中保留锁与构建缓存，先检查相关屏幕，交付前再跑要求的完整屏幕组合。验收场景支持手柄步骤及自动松键。

边界检查记录开始时的文件状态，检测之后的越界变更，包括原有脏文件再次被修改；不会还原文件。Git 工作区检查受版本控制及非忽略文件，独立 DevKit 检查 SDK、接口、配置与开发工具源码。发行工具链二进制由签名分发管理。它是修改审计，不是阻止任意 shell 写入的操作系统沙箱。构建/运行结果包含检查状态，越界时不能把 returncode 0 当成成功。任务结束可用 `scope end` 归档通过的检查，再开始其他工作；发现违规时不能清除记录。重复 setup 会先检查并归档原任务。

## AI 适配新板

把这句话交给 AI：

> 请读取 https://pxa.doit.am/board-agent.md，按我提供的原理图把这块板适配到 PXA。只修改新板目录及对应 Profile，不修改 PXA 接口、SDK 或其他板子。请实际构建并按授权完成设备验收。

在独立固件工作区配置板级工具（需要该工作区的 ESP-IDF；不会下载 App DevKit）：

```sh
uv run --python 3.12 https://pxa.doit.am/setup.py --workspace . --mode board
.pxa/pxa board init my-board --target esp32s3 --reference pai-touch
.pxa/pxa board check my-board
.pxa/pxa board build my-board
```

脚手架包含现有 board-port 注册入口、待填写的硬件清单和构建阻断提示；没有猜测 GPIO、供电或 Flash 分区。完成真实驱动前检查不会通过。允许修改本板 BSP、模拟器 Profile 和依赖管理器生成的本板 lock；SDK、ABI、共享板级层与其他设备仍只读。提供 `pxa-board-port` skill、可选 `pxa-board-developer` 角色以及对应 MCP 工具。新 CPU 或缺失的平台接口应作为独立平台任务，不能在板适配中偷偷改接口。

## 更直接的查询与商城截图

`context` 优先返回准确符号和声明，合并相邻重复片段；不指定查询时列出当前 SDK 头文件。调试直接使用同一 DevKit 的 PXADB，避免每次输入都加载构建 CLI。原生与容器使用同一验收流程。

```sh
.pxa/pxa verify apps/counter --profiles pai-touch --store-screenshots
```

商城截图模式通过 DevKit 的 `run --store-screenshots` 启动真实模拟器，使用无圆角矩形视口，关闭圆屏遮罩及安全边距，隐藏手势条和三按键导航，并关闭模拟器性能采样。应用自身绘制的性能浮层须在拍摄前关闭。逐张检查原始图片，确认完整矩形画面及浮层状态后上传，保留工具记录的原始图片摘要。禁止裁剪、修图或生成图替代实际截图及上述设置。MCP 对应参数为 `pxa_verify(store_screenshots=true)`。故障排查见 [AI 开发排障](https://pxa.doit.am/docs/ai-troubleshooting/)。

## 按锁定版本查接口

`context` 与构建使用相同的已安装锁定 SDK；版本缺失时明确提示安装，不回退到其他版本。C++ 检索包含随该 SDK 发行的 `extensions/*/sdk/include` 与扩展示例，例如 `context apps/counter --query FrameReader`。应用边界审计也保护扩展 SDK。网络短暂失败时只重试 GET/HEAD 读取，不自动重放上传、授权或资料修改。

开发期明确需要通用包时，可用 `pxa_build(project="apps/demo", targets=["esp32s3", "esp32s31", "esp32p4", "simulator", "windows-simulator", "wasm"])` 合包构建并复用公共编译结果；商城交付使用 `pxa_store_build` 分目标生成独立包。日常使用默认模拟器目标即可。任务轮询可用 `include_log=false`，需要诊断时再读取日志；已完成任务的耗时不会随轮询继续增长。

## 按设备设计与一句话上架

运行 `.pxa/pxa device-profile` 查看端口与 PXADB 开启指引，再用 `--port` 读取实际 CPU、分辨率、调试输入和传感器。请补充物理操作方式和自适应屏幕要求。MCP 使用 `pxa_device_profile`。像素游戏实现建议见随包 pxa-app-dev 的 pixel-games 参考：Z Labs 中文像素字体、整数缩放、输入换算、图块复用与异步存档。

上架时 AI 先根据实际产品起草名称和简介，补充无法确定的目标用户、素材授权及已有图标信息，再运行 `store prepare` 检查缺项。有生图能力则制作匹配图标和可选封面；没有则制作简单图标并跳过封面。截图必须使用 `verify --store-screenshots` 实际运行并逐张检查原图；首次浏览器登录后，后续提交自动复用账号授权。

## 测试构建、UI 诊断与 scope 恢复

```sh
.pxa/pxa verify apps/timer --profiles pai-touch --define TIMER_TEST_SECONDS=8 --require-ui
.pxa/pxa scope begin --mode app --project apps/timer --adopt
.pxa/pxa scope end --archive --allow-violations
```

`--define NAME[=VALUE]` 可重复，传给本次验收的每个 profile；测试宏只用于调试构建，
交付前重新构建无测试宏的正式包。MCP 使用 `pxa_verify(definitions=["TIMER_TEST_SECONDS=8"], require_ui=true)`。
`--require-ui` 检查 Host 导出的真实保留式节点树，不再用屏幕中央颜色变化推测内容。
顶部布局不会误判为空白；Canvas 节点也计入结构检查。Surface-only 应用不使用该开关，
需要核对真实画面和操作结果。结构检查不能代替视觉检查。

新增 `pxa_ui_inspect`、`pxa_tap_node` 和 `pxa_ui_expect`；CLI 对应
`device ui inspect|tap|expect|wait-for`。可读节点、父子关系、文字、值、可见/启用状态及
实际像素矩形；密码框内容被隐藏。`verify --scenario` 与 `pxa_interact` 均支持
`tap-node`（selector）、`expect`（selector、expected）和 `wait-for`（再加 timeout_ms）。
按类型/文字找到唯一按钮后点击，再断言显示状态；重名、隐藏、禁用或超时会失败。
详细字段和示例见 pxa-debug 的 scenarios 参考。旧 Host 没有该能力时明确失败，
需通过正常的显式 SDK/固件更新使用匹配版本，不修改安装目录或偷偷变更锁。

`result.json` 保留运行日志尾部、UI 错误、场景步骤耗时、语义检查结果及本次测试宏。已报告的 UI
挂载/更新错误会使验收失败；应用须声明 `log` 服务才能将 Guest 诊断输出到 Host 日志。旧版 SDK 没有完整的错误日志，需使用匹配的新 SDK 验证。

切换应用会提示新的 scope，并归档原来的基线。返回原应用时，`--adopt` 仅允许采纳
目标应用目录中的改动，保留之前失败的审计；SDK/平台改动仍阻断。若确需结束失败审计，
`end --archive --allow-violations` 保留原基线和违规列表，返回 `passed: false`，不修改源码。
无需手工编辑 `.pxa/change-scope.json`。`context --query Button` 也会按示例源码查找实际引用，
返回 `example_matches` 的文件、行号及代码片段，原有 `examples` 路径列表继续保留。

## 构建复用与多屏验证

`verify --jobs 1..4` 控制同时运行的屏幕数量，默认 2；每次先构建一次，再让各屏幕
校验并运行同一个包。测试状态、发布者公钥输出和调试接口逐屏隔离，不清除日常模拟器存档。
一屏失败后仍完成其他屏幕，并分别记录失败步骤。新建 C++ 应用附带 `tests/basic.json`，
可直接验证计数器；修改作品后同步替换为相应行为断言。

```sh
.pxa/pxa verify apps/counter --profiles pai-touch generic --scenario apps/counter/tests/basic.json --require-ui --jobs 2
.pxa/pxa verify apps/counter --profiles pai-touch generic --scenario apps/counter/tests/basic.json --require-ui --jobs 2 --no-build
```

`--no-build` 不编译、打包或更新锁；它检查源码/common 目录输入、实际 CMake/编译依赖、声明的生成器输入、资源、
SDK/工具链锁、CPU、测试宏、签名身份和包摘要。过期或缺少新格式构建记录时明确失败。
输入清单采用保守检查，应用目录内的输入文件变化后应先构建；测试宏须重复相同 `--define`。
可复用准备好的 Debug 或 Release 包，实际模式写入结果。MCP 对应 `pxa_verify(no_build=true,
profile_jobs=2)`。SDK 提供 `build --check-existing` 单独校验，以及 `run --no-build` 复用运行。

结果包含构建记录、所请求的屏幕参数、Host 实际指标（可获得时）、步骤耗时和原图摘要。
`contact-sheet.png` 只供快速检查，按实际操作顺序选择最后一张截图；它不是商城截图，
不进入原图清单。商城上传仍逐张核对并使用 `result.json` 的 `screenshots` 中的真实矩形原图。
大批次的完整语义诊断保留在本地，状态查询返回摘要，不因结果超过 64 KiB 而误判失败。

旧 SDK 没有受校验的 `--no-build` 时，普通 `verify` 仍先构建一次，再固定该包的字节和摘要，
通过它自己的原生模拟器并行运行，使用独立状态和当前设备客户端。结果会标明缺少源码输入清单，
不接受旧 SDK 的 `verify --no-build`；重新执行普通验证即可。实际 Host 未提供 UI 检查时，
语义断言或 `--require-ui` 明确失败，坐标操作与原始截图仍可用于视觉验收。
