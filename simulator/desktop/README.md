# Desktop Standard UI Simulator / 桌面标准 UI 模拟器

This is the standalone SDL2/LVGL host for the PXA standard system UI. It uses
only code and dependency declarations inside `pxa-system`; it does not use the
product repository's `app_pages`, fonts, assets or device services.
LVGL is a private simulator dependency and is not included in PXA System SDK
install/export output.

At configure time, Python reads `apps/pxa/*/package.json` and generates the
built-in launcher catalog. Launching one of those entries exercises the real
registry, Intent, task-manager and `pxa_system_pxa` lifecycle through the
lightweight `pxa-sim` backend. This default mode intentionally does not compile
or execute Guest bytecode, so the standard UI remains runnable without wasi-sdk
or `wamrc`. Use the product integration simulator when testing real WAMR Guest
service calls.

这是 PXA 标准系统 UI 的独立 SDL2/LVGL 宿主。它只使用 `pxa-system` 内部的
代码和依赖声明，不依赖产品仓库中的 `app_pages`、字体、资源或设备服务。
LVGL 仅为模拟器私有依赖，不会进入 PXA System SDK 的安装或导出内容。

配置阶段会用 Python 读取 `apps/pxa/*/package.json` 并生成内置启动器目录。
启动目录项会经过真实的 registry、Intent、task manager 和
`pxa_system_pxa` 生命周期，再由轻量 `pxa-sim` 后端显示。默认模式不编译或
执行 Guest 字节码，因此无需 wasi-sdk 或 `wamrc` 也能运行标准 UI；真实 WAMR
Guest 与产品 service 联调仍由产品集成模拟器负责。

```sh
cmake -S simulator/desktop -B build/desktop
cmake --build build/desktop
./build/desktop/pxsys_desktop_simulator --dark
./build/desktop/pxsys_desktop_simulator \
  --profile compact --locale zh-CN --launch pxa-weather
./build/desktop/pxsys_desktop_simulator \
  --profile round --custom-theme --launch pxa-lab \
  --duration-ms 500 --screenshot /tmp/pxa-round.png
```

For an offline checkout, point CMake at an existing LVGL 9.5 source tree:

离线环境可指定已有的 LVGL 9.5 源码目录：

```sh
cmake -S simulator/desktop -B build/desktop \
  -DPXSYS_LVGL_SOURCE_DIR=/path/to/lvgl
```

Profiles are `compact` (296x240), `phone` (390x844), and `round` (454x454 with
a circular display profile and safe insets). Options also include `--light`,
`--dark`, `--custom-theme`, `--gestures`, `--locale TAG`, `--width`, `--height`,
`--round`, `--safe-insets T,R,B,L`, `--launch APP_ID`, `--screenshot PNG`, and
`--duration-ms`. Deterministic fixtures can be selected with `--time HH:MM`,
`--battery 0..100`, `--network`, `--network-signal`, `--permission allow|deny`,
and `--storage-bytes`. `--smoke-test` and `--self-test` render briefly and exit for CI.
CTest covers every profile across light, dark, custom, English, and Simplified
Chinese cases and rejects blank or incorrectly sized screenshots.

内置 profile 包括 `compact`（296x240）、`phone`（390x844）和 `round`
（454x454、圆屏语义与安全区）。此外可使用 `--light`、`--dark`、
`--custom-theme`、`--gestures`、`--locale TAG`、`--width`、`--height`、
`--round`、`--safe-insets T,R,B,L`、`--launch APP_ID`、`--screenshot PNG`
与 `--duration-ms`。确定性夹具可通过 `--time HH:MM`、`--battery 0..100`、
`--network`、`--network-signal`、`--permission allow|deny` 和
`--storage-bytes` 设置。`--smoke-test` 和 `--self-test` 用于 CI。CTest 会覆盖三种
尺寸、明暗/自定义主题和中英文，并检查截图尺寸与非空像素。

Set `PXSYS_DESKTOP_APP_SOURCE_ROOT` to generate the catalog from another PXA
application tree. The source root must contain one `package.json` per app.

可通过 `PXSYS_DESKTOP_APP_SOURCE_ROOT` 从其他 PXA 应用树生成目录；每个应用
目录必须包含一个 `package.json`。
