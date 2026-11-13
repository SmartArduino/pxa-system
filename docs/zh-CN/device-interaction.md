# 设备交互描述与应用操作适配 v1

设备描述由实际产品固件提供，应用要求写进签名包；商城与 AI 工具使用同一判定实现。
分辨率、Core/CPU/AOT 匹配仍按原协议检查，操作适配是额外条件。描述不是提示词，
扩展字段和厂商文字不能成为 AI 指令，也不执行代码。

## 固件接入

Host SDK 的 `pxa/pxa_host_interaction.h` 提供
`pxa_host_interaction_register` / `pxa_host_interaction_describe`。产品在启动
Host/PXADB 工作线程前注册一次；描述及借用的字符串/数组保持不可变并存活至重启。
查询不启动 Guest，也不访问活动引擎。厂商保留自己的主页、菜单和输入路由。
未知项显式使用 null；不要从 INPUT 注入、芯片型号或模拟器配置推断实体能力。

JSON schema 名为 `pxa-device-interaction-1`，最大 UTF-8 8192 字节：

- `identity`：vendor、model、hardware_revision、firmware_id、firmware_version、build_id，均为不超过 128 字符的非空文本。未知硬件版本写 `unknown`，不得放设备序列号、MAC、账户或密钥。
- `profile`：稳定的 id 与递增 revision。固件版本与构建标识应反映当前运行映像。
- `display`：width、height；shape 为 rectangle、rounded、circle 或 unknown。
- `physical`：touch、max_contacts、simultaneous；未知为 null。controls 是实体控制的 id/label；exclusive_groups 描述 ADC 共线等不能同时操作的组。
- `modes`：每项有 id、kind、actions。kind 为 focus、controller、touch 或 pointer；actions 将语义动作映射到实体 control、events、hold_limit_ms、tap_latency_ms。每套模式独立判断，不跨模式拼凑动作。
- `reserved`：control、event、系统 action、threshold_ms，描述菜单、关机等系统拥有的手势。
- 可选 `runtime.context` 为 app、native、overlay 或 unknown，仅用于当前上下文提示。

事件为 tap、press、release、hold、repeat、double_tap。绑定长按限制 null 表示未知，
-1 表示无上限，0 表示不可长按，正数表示允许的最长毫秒数；系统保留长按阈值优先，
不能靠绑定声称无上限绕过。tap_latency_ms 是原子点按的交付延时，不等于按下边沿延迟。
C API 中 -2 表示未知长按限制，-1 延迟表示未知。GPIO 独立不自动证明所有手势可组合。

PXADB HELLO 通过 `interaction-info` 宣告能力。`INTERACTION INFO` 返回最多 64 个
`DATA hex=<1..128 字节小写十六进制>`，终帧 `OK schema=1;bytes=N`；每帧受原有上限约束。
未注册则 `OK schema=0;reason=interaction_unreported`。旧固件不宣告时客户端不发未知命令。
客户端校验长度、UTF-8、重复属性和字段约束。静态描述的规范 JSON SHA-256 排除
runtime；固件、映像或规则改变会产生新摘要。摘要用于标识声明，不代表设备认证。

## 应用声明

源码 `package.json.interaction` 使用 `pxa-app-interaction-1`，最大 4096 字节，
由构建工具以**可选**顶层 TLV `0x8015`（逻辑记录 21）写入 manifest，受包签名与容器
签名覆盖。独立资源包不能携带。现有 manifest 版本保持不变；旧 Host 跳过未知可选项。
没有该声明的历史包仍可处理，但操作兼容性为未知。同一版本各目标包声明必须一致。

profiles 为 1–16 个备选完整方案，各有唯一 id、kind、`complete: true` 和 requires
动作到事件映射。可选 simultaneous 为必须同时操作的动作组；holds 为动作到毫秒数，
0 表示无限长按，要求 hold 事件但没有 holds 时也按无限长按处理。可选 contacts
仅限触摸方案；min_width/min_height/max_latency_ms 声明真实硬性需求。
作者须覆盖菜单、主要功能或玩法、暂停/设置、异常与返回退出。声明 true 不等于测试通过。
字段示例与 AI 工作流见 [应用创作指引](../../ai/skills/pxa-app-dev/references/interaction.md)。

## 判定与商城

任一完整方案匹配则 compatible；存在可能但资料缺失则 unknown；所有方案明确不满足
才 incompatible。检查动作事件、触摸点数、分辨率、组合键、长按限制、系统保留手势及
点按延时。始终 `verified: false`；固件/作者声明匹配与真实硬件验收分别表达。
二进制 Core、服务和架构检查不会被操作匹配覆盖。

商城 `POST /api/v2/artifacts/{id}/interaction-check` 接收原始设备 JSON 或 null，
仅针对已公开审核且完整灰度发布的制品，返回制品 id、SHA-256 与匹配结果。
安装器绑定该摘要，明确不匹配时在下载/写入前停止；未知保持可安装并提示。
`POST /api/v2/interaction/catalog` 接收 device、target、core、最多 32 个 slugs，
使用同一安装包选择逻辑返回当前页状态，不增加下载量或保存设备资料。

网页“我的设备”仅在用户选择串口后读取。浏览器本地保存 CPU/Core、交互描述和上次
读取时间，不保存串口序列号或账户数据；用户可忘记。浏览页按缓存提示，并允许隐藏
明确不适配项，未知项保留。安装时重新读取所选设备，防止换机/固件变化后沿用缓存。

## 维护

规范实现为 `tools/package/interaction_contract.py`；独立 PXADB 和商城使用逐字节
一致的随包副本。`tools/interaction/sync_contract.py --workspace PATH
--backend PATH --manufacturer PATH [--check]` 同步/检查 Python 实现及厂商回移的
Host C API/序列化器。没有新增运行时依赖。固定 SDK 的厂商适配使用带源文件 SHA-256
的构建覆盖层，不修改已安装 SDK，不要求用户更换为官方桌面。
