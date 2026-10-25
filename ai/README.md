# PXA Agent Kit 0.3.3

[English](README.en.md) · [一句话上手](https://pxa.doit.am/docs/ai-development/) · [AI 入口](https://pxa.doit.am/ai.md)

把这句话交给 Codex、Claude Code、DSH 或能读网页并执行命令的编程助手：

> 请读取 https://pxa.doit.am/ai.md，在当前目录为我配置 PXA 开发环境，运行模拟器检查环境，然后等待我的应用需求。

工具在电脑上运行。已有 AI 账号/客户端可直接使用，无需另配模型 API Key、ESP-IDF、CMake 或 LLVM。SDK、工具链、模拟器按官方签名与 SHA-256 校验。首次下载较大，之后离线构建。Linux x86_64/glibc 2.39+ 可原生运行；macOS、Windows、较旧 Linux 使用 Docker Desktop 或 Podman 的 Linux amd64 容器。首次安装容器引擎可能需要操作系统权限或启用虚拟化，登录 AI 和浏览器商店账号仍由用户完成。

## 命令入口

安装 [uv](https://docs.astral.sh/uv/getting-started/installation/) 后，一条命令准备独立工作目录：

```sh
uv run --python 3.12 https://pxa.doit.am/setup.py --workspace ./my-pxa
```

或使用源码/已经校验并解压的发行包：

```sh
uv run --python 3.12 --locked --project ./ai ./ai/agent.py --workspace ./my-pxa setup
```

`setup` 自动选择官方 DevKit 0.5.0 或签名容器镜像，安装项目内的 skills、平台 MCP 配置、CLI 启动器，最后编译并运行真实 Guest、保存检查截图。失败时返回失败，不会把下载成功当成验收。`--sdk /path/to/devkit` 仅复用用户自己选择的完整 SDK，不宣称它经过下载签名校验。离线分发可以用 `setup.py --channel /path/to/ai-kit-0.3.3.json --manifest /path/to/distribution.json`，归档和签名放在各自清单旁。

在 `my-pxa` 目录中使用以下命令（Windows 使用 `.pxa\pxa.cmd`）：

```sh
.pxa/pxa doctor
.pxa/pxa init apps/counter --id pxa-counter
.pxa/pxa build apps/counter
.pxa/pxa run apps/counter --profile pai-touch
.pxa/pxa verify apps/counter
```

`run` 启动实际桌面 Guest；容器在后台运行，用 MCP 截图与输入工具交互。`verify` 默认覆盖五种屏幕，返回日志、截图与 `result.json` 的路径。检查截图后再判定 UI 正确。测试数据写入工作区被忽略的 `local/artifacts/`，不会进入应用资源包。应用保存在 `apps/<name>`。

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

一组已知的连续操作可用 MCP `pxa_interact(job_id, steps)`：复用同一个模拟器调试连接，依次点击、滑动、按手柄、等待或截图。最多 32 步、8 张截图、累计等待/按住 15 秒；完整检查通过后才操作。返回批次 job_id 和证据路径，等任务结束再用 `pxa_view_image` 检查原图。需要观察中间结果时拆成短批次，同一模拟器不并发输入。CLI 可用 `verify --scenario`。

批次结束后，`pxa_job_status` 直接附带 `batch` 结果。停止批次用 `pxa_job_stop`：等待执行进程取消并松键，正常取消后模拟器可继续使用；执行进程无响应时才停止它所属的模拟器。取消不算验收通过，继续前先检查当前画面，不重放整批操作。

MCP 长操作返回 job_id，通过 `pxa_job_status` 获取结果，`pxa_screenshot`/`pxa_view_image` 返回可供 AI 直接查看的图片。关闭 server 时清理它自己启动的进程/容器。CLI 是同一实现的后备入口，不依赖 MCP 是否启用。

## 真机与官方固件

```sh
.pxa/pxa doctor
.pxa/pxa device info --port /dev/ttyACM0 --baud 2000000
.pxa/pxa install apps/counter --port /dev/ttyACM0
.pxa/pxa firmware prepare --board doit-gamekit-1
.pxa/pxa firmware upgrade --board doit-gamekit-1 --port /dev/ttyACM0
```

先发现串口并核对 INFO 的板型、CPU，再构建匹配的 S3/S31 AOT；安装保留已有应用数据。截图、按键、内存使用 host 端 PXADB，无需给容器转发 USB。控制器注入在操作后自动 `INPUT CANCEL`，模拟按键不声称为机械按压。

固件默认 app-only，校验官方签名、镜像摘要与设备实际分区表后才写入，不碰数据分区。分区变化会停止，例如旧版 Mosaico 升级到 0.5.0。备份并获得用户明确清数据授权后，才使用 `--mode full --erase-data`。S31 可能需要按住 BOOT、按 RESET、松 BOOT 进入 ROM，写完按 RESET；端口可能变化。写入完成和重启后 INFO 验收分别报告。

## 上架 app.doit.am

```sh
.pxa/pxa store authorize apps/counter apps/counter/dist/pxa-counter.pxa --wait
.pxa/pxa store publish apps/counter apps/counter/dist/pxa-counter.pxa --changelog '首次发布'
```

首次打开浏览器，登录并确认该应用的短码授权。工具在本地证明持有签名私钥，服务器登记公钥、创建/复用你的应用资料，并发放仅限该应用、有效 30 天、可在后台撤销的上传权限。私钥不上传，令牌不进入聊天、参数或源码；后续自动复用。`store.json` 可设置 name/summary/description/kind/category；已有商店资料不会被自动覆盖。

`store publish --icon icon.png --screenshot screen.png` 可同时上传素材。最终确认本地包摘要与服务器接收摘要一致，状态为 pending_review；审核通过后才公开。准备多目标设备包时先按实际受支持目标构建，再上传，不用桌面包冒充真机包。

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

商城截图模式在启动模拟器前隐藏手势条和三按键导航，记录原始图片摘要。先关闭所有性能浮层，再逐张检查原图；不能裁剪或修图代替关闭系统界面。MCP 对应参数为 `pxa_verify(store_screenshots=true)`。故障排查见 [AI 开发排障](https://pxa.doit.am/docs/ai-troubleshooting/)。
