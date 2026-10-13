# PXA AI 开发套件 0.1.0

[English](README.en.md)

可选的 skills、Codex agent 配置和自动验证脚本，配合独立 DevKit 0.3.0 开发 C++26 应用。无需固件工作区、额外 MCP 服务或 Python 第三方库；不会在设备上安装 AI、增加 Guest 内存或启动常驻代理。Python 需 3.11+，也可使用 DevKit 自带的 `runtime/python/bin/python3`。

## 安装与加载

从 [GitHub Release](https://github.com/SmartArduino/pxa-system/releases/tag/ai-kit-v0.1.0) 下载 `pxa-ai-kit-0.1.0.tar.gz`，解压后在项目父目录运行：

```sh
python3 pxa-ai-kit-0.1.0/install.py list
python3 pxa-ai-kit-0.1.0/install.py install --project hello --agents --dry-run
python3 pxa-ai-kit-0.1.0/install.py install --project hello --agents
```

`hello` 必须已存在；可以先执行 `pxa init hello`。技能写入 `hello/.agents/skills/`，两个可选 agent 写入 `hello/.codex/agents/`。安装器不改 `AGENTS.md`、`config.toml`、全局目录、权限或模型。源码检出可直接用 `ai/install.py`；后续完整 DevKit 也会携带 `ai/`。

只装某个技能：`install --project hello --skill pxa-debug`；重复安装相同内容无操作。升级使用 `install --project hello --agents --update`，只替换套件拥有且未被用户修改的文件。`remove --project hello` 只移除套件记录的文件，遇到用户改动会保留并报错。先移动需要保留的修改，再执行移除。不要同时运行两个安装器。

Codex 会发现项目内的技能；未出现时重新打开项目/任务。也可使用 skill-installer 从 `SmartArduino/pxa-system` 的 `ai/skills/<name>` 路径加载单个技能；其他兼容 Agent Skills 的工具可以加载相同文件夹。根目录 `plugin.json` 提供便携插件结构，但这里不承诺自动进入任何公共插件目录。

## 让 AI 做什么

| 名称 | 用途 |
| --- | --- |
| `$pxa-app-dev` | 创建或修改应用，查阅真实 C++26 SDK，完成构建与交互验证 |
| `$pxa-debug` | 定位失败，跨五种屏幕运行，生成日志、截图和 JSON 证据 |
| `$pxa-device-test` | 指定串口真机验收，核对 Core/AOT、内存和清理测试包 |
| `$pxa-release` | 验证签名包、公钥登记、发布准备和中英双语说明 |
| `pxa_developer` / `pxa_tester` | Codex 的开发与独立验收角色，继承用户的模型与权限 |

在项目中向 AI 提出具体请求即可，例如：

> 使用 $pxa-app-dev 写一个倒计时应用，适配五种屏幕。使用 $pxa-debug 构建并启动真实 Guest，验证开始、暂停和重新开始，检查截图；遇到问题自行修复。DevKit 路径是 /path/to/devkit。

需要独立验收时明确要求“让 pxa_developer 实现，再让 pxa_tester 验收”。agent 配置只定义职责，不改变工具的模型、权限或自动委派策略。没有自定义 agent 功能的工具，仍可用相同技能完成单 agent 开发；TOML 的 `developer_instructions` 也可作为角色指引手动加载。

## 自动验证脚本

从项目父目录运行：

```sh
python3 hello/.agents/skills/pxa-debug/scripts/verify_app.py hello \
  --pxa /path/to/devkit/bin/pxa --output hello-evidence --headless
```

默认覆盖 pai-touch、SenseCAP Watcher、esp-mosaico、Korvo-1 和 generic。输出目录必须不存在，且位于项目之外。脚本使用正常的项目构建缓存和模拟器存档，不清除已有状态；干净状态用独立测试项目。缺少锁定工具时可显式加 `--online`。

`result.json` 的 `runtime_passed` 表示启动、活动身份、截图和所选输入命令通过；必须再检查截图才能证明界面正确。验证总耗时不是游戏 FPS、触摸延迟或音频延迟。模拟器不支持的 ESP 堆内存会记录为 unavailable。交互脚本格式见 [说明](skills/pxa-debug/references/scenarios.md)。

## 发行验证

AI 套件独立版本号与签名清单，不改已发布的 DevKit 0.3.0 或其清单。先校验 Release 提供的 `SHA256SUMS`，并使用可信 DevKit 的发行公钥验证 `ai-kit-0.1.0.json.sig`；清单的 archive.sha256 是归档摘要：

```sh
"$PXA_DEVKIT/runtime/bin/openssl" dgst -sha256 \
  -verify "$PXA_DEVKIT/config/release-public.pem" \
  -signature ai-kit-0.1.0.json.sig ai-kit-0.1.0.json
sha256sum pxa-ai-kit-0.1.0.tar.gz
```

安装器还核对包内文件清单，检测损坏；包内校验不能替代发行签名。加载技能不授予硬件刷写、生产发布或读取秘密的权限；沿用用户已经明确授权的范围。

格式依据：[Codex skills](https://developers.openai.com/codex/skills/)、[自定义 agents](https://developers.openai.com/codex/subagents/)、[便携插件](https://developers.openai.com/plugins/build/plugins)。本轮实际结果见官网的 AI 开发教程及工作区验证报告。
