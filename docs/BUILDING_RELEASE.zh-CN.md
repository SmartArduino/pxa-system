# Linux 发行构建与验收

发行以 `VERSION`、`config/devkit-linux-x86_64.json`、`config/wamr.json` 和工作区依赖锁为输入。SDK 与模拟器来自同一干净源码提交；发行包记录 system/workspace 提交、AOT ABI、工具链身份、全文件 SHA-256、运行库版本、许可证和 CycloneDX 1.6 SBOM。

## 构建环境

使用 Ubuntu 22.04 / glibc 2.35 的 Linux x86_64 构建环境。安装 Bash、coreutils、util-linux、Git、Python 3、CMake、Ninja、Clang 14/lld 14，以及 SDL2、PNG、FreeType、curl、OpenSSL、LZ4、Vorbisfile、Opusfile 和 zstd 的开发包。运行库收集要求对应 Debian 包的版权文件；精简镜像删除了 `/usr/share/doc` 时应重新安装匹配版本的软件包。

WAMR 使用仓库锁定的子模块提交。 首个候选版的定制提交已公开到 `lucinhu/wasm-micro-runtime`，源码 URL 与该固定提交一致；不依赖构建者的本地镜像。独立检出 `config/wamr.json` 中固定的 Espressif LLVM 提交，保留用户原有源码。LVGL 使用工作区 `firmware/dependencies.lock.pai-touch` 的来源和版本。编译器仅启用 X86、RISCV 和 Xtensa，覆盖本轮宣称的三个应用目标；不宣称其他交叉目标。

```sh
PXA_RELEASE_JOBS=8 bash tools/release/build_linux_baseline.sh \
  /path/to/pxa-workspace /path/to/output /path/to/pinned-llvm /path/to/pinned-lvgl
```

脚本构建原生模拟器及 wamrc、运行桌面测试，并收集动态库闭包。它不复制系统 glibc 或 loader，拒绝缺少库或版权说明的环境。代码保持 Release/O3；Guest 与 AOT 编译也使用 O3。

按照输入清单下载指定 Python build-standalone 和 WASI SDK 归档，核对清单 SHA-256 后解压。用该 Python 在独立目录安装清单中的四个精确 Python 包版本；不要复制用户的整个 Python 环境。保留上游 CPython/第三方许可证、WAMR/LLVM、WASI SDK/wasi-libc、LVGL 和字体 OFL 说明。二进制清单记录实际分发文件哈希；首次候选尚未承诺从任意时刻的系统包仓库构建得到逐字节相同的二进制。

```sh
python3 tools/release/build_devkit.py \
  --workspace /path/to/clean-workspace \
  --simulator-dir /path/to/output/simulator \
  --host-runtime /path/to/output/host-runtime \
  --wamrc /path/to/output/wamrc/wamrc \
  --wasi-sdk /path/to/wasi-sdk \
  --python-root /path/to/python-runtime \
  --python-site /path/to/pinned-python-packages \
  --licenses /path/to/collected-license-notices \
  --output /path/to/pxa-devkit-0.2.0-rc.1-linux-x86_64
```

默认拒绝 system 或 workspace 的未提交输入。`--allow-dirty` 只供开发调试，不得上传这些构建。私钥、用户状态、构建缓存不属于发行输入。每次发布使用新的目录和版本，禁止覆盖已发布的版本资产。

## 发布门槛

```sh
python3 tools/release/metadata.py check
python3 spec/draft/tools/check_spec.py
python3 tools/release/verify_devkit.py --devkit /path/to/kit --output /path/to/new-results
python3 tools/release/split_assets.py --devkit /path/to/kit --output /path/to/new-assets
```

验收脚本在源仓库外、含空格的路径构建 C/C++ 三目标包，核对签名、锁、真实能力、兼容拒绝、输入、截图及重复退出。还需在 glibc 2.35 的环境，仅挂载只读 DevKit 与可写测试目录、关闭网络、使用空 HOME/PATH，重复验收；它不能使用源码、IDF、系统编译器或开发机缓存。用真实归档测试 `pxa sdk install`，移动安装路径后重新构建和运行。分别测试小型 SDK 和独立模拟器资产。

真机验收使用设备自己的能力查询、安装与实际显示；保留原分区和应用数据。测试新的 C/C++ 包在旧/新 Host 运行，旧二进制在新 Host 启动；采集截图、输入响应和当次内存峰值。测试后移除本轮创建的应用、恢复临时设置及原固件。缺少能力查询的旧 Host 明确记录该限制，不能用伪造能力文件代替实测。

原生和 SDK 回归、安装拒绝测试、基准系统门槛、独立开发流程、真机恢复检查及发行哈希全部通过后，才创建 Git 标签与 GitHub 草稿。上传 DevKit/SDK/模拟器、SHA-256 和验收报告，重新下载核对，再发布 prerelease。CI `release-gates.yml` 提供源码回归；它不代替离线二进制和真机认证。

归档按提交时间或 `SOURCE_DATE_EPOCH` 归一化 owner、mtime 和 gzip 头；相同冻结的输入与文件树可生成相同归档。编译器构建路径、系统包及签名时间不同的独立构建是否位一致，需要另外证明。
