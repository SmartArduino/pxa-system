# 独立 C++26 DevKit

PXA 0.3.0 提供 Linux x86_64（glibc ≥ 2.39）开发套件。Guest SDK、CLI 和模拟器
共用发行号；Core 2.0、服务版本、Manifest/Container 版本及 AOT engine ABI
各自独立。0.2 系列的旧 Core 应用需要重新编译，不承诺直接二进制兼容。

## 开发入口

解压 `pxa-devkit-0.3.0-linux-x86_64.tar.gz`，将其 `bin` 加入 PATH：

```sh
pxa init hello
pxa run hello
pxa run hello --watch
pxa run hello --profile sensecap-watcher
pxa build hello --target esp32s3,esp32s31,simulator --offline
pxa run hello --device /dev/ttyACM1 --baud 2000000
pxa publish hello/dist/pxa-hello.pxa --check-only
pxa publish hello/dist/pxa-hello.pxa --token-file /path/to/pxa-upload-token.json
```

`init` 创建普通 CMake C++26 工程，使用 `find_package(Pxa CONFIG REQUIRED)`。
设备命令读取真实 CPU 目标、安装签名包并核对实际激活状态。开发工具不替代
Host 的签名、Core、服务能力、资源预算及 AOT ABI 校验。

模拟器支持 `--duration-ms` 和 `--screenshot` 自动化。截图在正在运行的 Guest
画面上采集，随后退出；避免退出时覆盖成应用列表。模拟器不预测 ESP 的帧率。

使用 `pxa run hello --debug` 开启独立 PXADB 调试入口；命令会打印该实例的
`--simulator` 地址。`pxadb package run/stop/list` 操作当前 Host，并报告真实
活动状态；也可注入触摸和获取截图。普通运行不启动额外调试进程。

`--watch` 检查项目、相邻 common 目录和 SDK 文件的修改，合并连续保存。
先构建并验证完整新包，编译失败时继续运行原应用。模拟器成功更新会重启
Host/Guest；应用自己的文件存档保留，内存中的游戏状态不会自动迁移。

## 锁定、缓存与身份

首次构建生成可提交的 `pxa.lock`，包含 SDK 内容摘要、WASI 版本、Clang/wamrc
摘要、固定编译配方及 Core 要求，不含机器绝对路径、私钥或令牌。
已有项目不会随最新版本升级：只有 `pxa sdk update hello` 或显式
`--update-lock` 更新锁定。不同发行版本已安装时，工具自动选取锁定的 SDK；
相同版本内容不同时拒绝继续，避免把不同源码伪装为同一个正式版。

同一 CMake 配置只构建一次 Wasm。AOT 按 Wasm、编译器、CPU/flags 缓存，资源
缓存追踪实际声明的源文件与调色板；签名容器按完整清单、文件、SDK 和发布者
身份缓存。不变构建复用原签名包及其摘要；输出以完整 generation 原子切换。
`.pxa.build.json` 记录耗时、配置和摘要，`.pxa.provenance.json` 记录实际工具。

缓存在项目 `.pxa`，输出在 `dist`；SDK 安装目录可只读。自动生成的开发私钥
在用户数据目录 `pxa/identities/development.pem`，权限 0600，可用
`--signing-key` 或 `PXA_SIGNING_KEY` 指定既有发布身份。不要把临时开发身份当作
现有应用的升级身份。商城只接受已经登记/验证的发布者，上传令牌按应用授权，
可撤销、30 天过期，并随账户会话版本失效。发布命令验证包后逐块上传原文件，
不重签、不改 AOT、不重写清单；商城仍完成独立签名与发布身份审核。

## 在线与离线发行

```sh
pxa sdk install 0.3.0
pxa sdk install 0.3.0 --offline --manifest /media/0.3.0/distribution.json
```

两者读取同一份清单、签名及五个组件：sdk、devtools、runtime、toolchain、
simulator。内置分发公钥校验清单，再校验每个组件大小/SHA-256、安全解包并
原子安装到用户数据目录 `pxa/releases/0.3.0`。已安装版本不可覆盖。
首次完整包仍需通过可信官网/发行页取得，之后安装以其内置公钥为信任根。
App 发布者密钥和 DevKit 分发密钥是不同用途的身份，不随 SDK 分发私钥。

维护者使用产品工作区 `tools/release.py`，在固定 Ubuntu 24.04 镜像内构建、
测试和组装。普通 App 开发不编译 LLVM。`tools/release/metadata.py --check`
检查 VERSION 派生文件及商城协议目录；默认组装拒绝脏源码，临时验证才可
使用 `--allow-dirty`。清单记录系统/工作区提交，组件摘要、编译配方和宿主
支持范围；构建环境软件清单及许可文件随发行提供。

发布前验证空白环境、移动目录、带空格路径、只读安装、离线构建、缓存失效、
模拟器实际画面、真机激活以及商城迁移。所有文件上传完整后才公开发行；
正式版本和同名资产不覆盖。Linux ARM、Windows、macOS 不属于本次二进制
认证范围，需分别构建、封装并通过相同验收后发布。
