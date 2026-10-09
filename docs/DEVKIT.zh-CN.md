# PXA 应用开发套件

官方 DevKit 包含 C/C++ SDK、WASI Clang、PXA 定制 wamrc、CMake/Ninja、Python、打包签名工具、PXADB 和 SDL 模拟器。应用开发不需要检出 PXA Host、板级工程、ESP-IDF 或 WAMR 源码。首次安装需要下载发行包，常规编译和模拟器运行无需网络。

首个候选发行面向 Linux x86_64，glibc 2.35 或更新。需要系统提供 Bash、常见 coreutils、可用的 X11/Wayland 显示环境；无桌面的 CI 可使用 SDL dummy 驱动。Windows、macOS、ARM Linux 尚不在二进制支持范围。交叉编译目标包括模拟器、ESP32-S3 和 ESP32-S31；真机运行还取决于设备实际服务能力、AOT ABI、资源预算和发布者信任配置。

## 仓库外开发

将压缩包解压到任意目录，把其中的 `bin` 加入 PATH。开发套件可以移动，项目路径可以包含空格，SDK 目录可以只读。

```sh
export PATH="/opt/pxa-devkit-0.2.0-rc.1-linux-x86_64/bin:$PATH"
pxa --version
pxa doctor --verify
pxa init hello --language cpp
cd hello
pxa build --target simulator
pxa check dist/hello.pxa
pxa run --sim --profile pai-touch --pxadb
```

`--language c` 创建 C 模板。默认优化为 O3。`pxa build --target simulator,esp32s3,esp32s31` 将多个 AOT 和 Wasm 后备产物一起签名打包。`dist/hello.pxa` 是分发容器，`dist/pxa-hello` 是可检查的目录，旁边的 provenance JSON 记录发行与工具链身份。

`pxa.lock` 锁定准确的发行包与工具链；它不是 Host 固件版本要求。将锁文件和 `package.json` 一起提交。`pxa sdk update` 是显式升级，只更新开发套件身份，保留应用原有兼容要求。构建会核对完整分发文件清单；修改这些文件需要创建新发行包，不能沿用旧锁。`pxa check --publisher-key publisher.der` 可以检查另一个明确受信任的发布者，默认检查当前用户的开发身份。

## 独立安装与签名

```sh
pxa sdk info
pxa sdk install --version 0.2.0-rc.1
pxa sdk install --archive ./pxa-devkit-0.2.0-rc.1-linux-x86_64.tar.gz --sha256 '<发行页提供的 SHA-256>'
```

安装不覆盖已有 SDK。安装后使用新目录的 `bin/pxa`；再显式执行 `pxa sdk update` 升级项目。归档和内部文件清单都会验证，解压会拒绝目录穿越和逃逸链接。

开发工具的 HTTPS 使用随 Python 分发的公共 CA 证书；即使基础系统未安装证书包也能下载。已有 `SSL_CERT_FILE` 配置优先保留，便于使用自己的信任配置。

首次创建项目生成用户独立的 P-256 开发密钥，默认保存在 `~/.local/share/pxa/keys`。可用 `PXA_USER_HOME` 指定用户目录，或用 `PXA_SIGNING_KEY` 指向已有开发私钥。私钥不进入项目、发行包或 provenance。保管并备份密钥；更换密钥意味着新的发布者身份。生产签名、发布者轮换及应用商店策略仍沿用 PXA 原有信任体系。

## 真机与模拟器

```sh
pxa device devices
pxa device info --port /dev/ttyACM0
pxa device runtime --port /dev/ttyACM0
pxa check dist/hello.pxa --device-port /dev/ttyACM0
pxa device install dist/hello.pxa --port /dev/ttyACM0
pxa device run pxa-hello --port /dev/ttyACM0
pxa device stop pxa-hello --port /dev/ttyACM0
```

设备必须信任对应发布者才能安装。已有开发固件可以继续使用其原有受信任密钥。套件不会自动修改设备信任、擦除数据或刷新分区。旧固件缺少 `RUNTIMEINFO` 时，能力查询明确失败；可导出新固件的能力 JSON，传给 `pxa check --host-profile FILE`。不要用另一台设备的能力文件替代实际验证。

`generic`、`pai-touch` 等 profile 只选择屏幕、圆角、安全区和 DPI，不改变运行目标：桌面始终使用本机 AOT。可以将原生模拟器参数放在 `pxa run -- ...` 后，例如 `--pxadb-control-socket /tmp/hello.control`。PXADB 支持截图和输入注入，发行验收脚本演示了无需桌面的自动化测试。

## 兼容要求

应用在 `package.json` 声明 `min_core`、各组件的服务最低/最高版本和必须功能位。旧的 `min_sdk`/`target_sdk`/`compile_sdk` 字段继续接受，它们表示 Core 协议版本，不是 SDK SemVer。新旧别名同时填写且值冲突会拒绝。

模板将服务简写解析为明确范围。降低最低服务版本必须核对实际使用的 opcode、字段及功能位，再对真实旧 Host 测试；候选版本没有自动分析所有源码调用的能力。`pxa check` 先原生验证签名和内容哈希，再检查 Core、服务版本、功能位、内存模型及 AOT ABI。它给出兼容的产物选择；运行资源额度和最终激活由 Host 校验。

更多版本策略见 [VERSIONING.zh-CN.md](VERSIONING.zh-CN.md)，构建与验收流程见 [BUILDING_RELEASE.zh-CN.md](BUILDING_RELEASE.zh-CN.md)。二进制支持和实际验收结果以对应 GitHub Release 报告为准。
