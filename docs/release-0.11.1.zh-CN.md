# PXA 0.11.1 与 Agent Kit 0.7.4

状态：发布准备中。公共安装入口保持已发布版本，候选制品等待发布授权。

本版完善设备调试、权限配置和语言保存，并更新独立 DevKit 与 AI Kit。
Guest Core 保持 2.1，原生 Host C ABI 保持 2；Guest 主循环仍按原有协作模型执行，
Host 原生工作任务可使用双核。应用兼容性继续以服务和特性协商为准。

## 设备与权限

固件可同时配置原生 USB 与 UART PXADB，两路保留各自的解析、日志与输入会话，
安装操作统一协调。自适配板可通过协议提供板名和设备序列号；网页安装面板先展示
已授权的端口，USB 转串口设备连接后再识别，浏览器授权选项由浏览器管理。

`PXA_ENABLE_PERMISSIONS=OFF` 可在 CMake Host 构建中移除权限策略、持久化与请求
界面并全部允许；ESP-IDF 固件使用 `CONFIG_PXA_ENABLE_PERMISSIONS=n`。
默认启用权限系统。设置语言时及时保存，避免首次语言修改尚未落盘便关机。

设备商城的完整版与轻量版改为精选优先；相应应用与商城后端随协调发布启用。
完整固件包含系统字体与商城。已有设备使用应用镜像升级前须核对分区表，保留
原有应用、私有数据和设置；完整镜像用于首次安装或经过备份的布局迁移。

## 工具与创作

DevKit 0.11.1 与 Agent Kit 0.7.4 带入最新 PXADB 客户端。POSIX 串口写入遇到
`EAGAIN` 时交还控制权，命令与上传沿用已有超时和取消路径。AI Kit 默认使用
DevKit 0.11.1，继续支持签名离线清单、项目 SDK 锁、真实 Guest 验证和矩形商城截图。
计时器配方收紧间距与外边距，给圆屏安全区域中的标题与完整按钮留出空间。
调试技能补充部分裁切检查，并区分设备默认布局与关闭遮罩、边距后的商城截图。

新增 `reaction` 游戏配方：30 秒内选择 10 个目标，含触摸与手柄、暂停/继续、胜负与重开。
生成真实节点的完整玩法和超时场景，不写入存档；测试宏与正常 30 秒版本分开构建。

源码工作区的应用打包器显式选择 Release，与独立 DevKit 默认一致；增量缓存重新
配置后采用相同的优化与断言策略。`PXA_KEEP_WASM_DEBUG=ON` 仍可保留调试信息。

Linux x64、Windows x64 与 Linux amd64 容器分别提供独立制品。Linux 原生包要求
glibc 2.39+；Windows 的 Wine 验证范围不包含原生 Windows 串口、音频与窗口验收。
Browser Host 独立编号，固件的商城能力快照保持 `0.11.0`。

## English

Preparation only; public installation channels retain their published versions.
PXA 0.11.1 and Agent Kit 0.7.4 update the bundled PXADB client, preserve serial
command deadlines under POSIX backpressure, and improve device integration.
Guest Core remains 2.1 and native Host C ABI remains 2. Guest execution remains
cooperative, while native Host workers can use both cores.

Firmware supports independent native USB and UART PXADB sessions with coordinated
installation operations. Permission policy, persistence and prompts can be compiled
out with `PXA_ENABLE_PERMISSIONS=OFF`, or `CONFIG_PXA_ENABLE_PERMISSIONS=n` on ESP-IDF;
the default keeps permission enforcement enabled. Language changes are saved promptly.
Factory images include a font and Store. Compare partition maps before updating only
the application; merged provisioning images replace device data.

Full and Lite Store apps use Featured as their default view with the coordinated
backend update. Linux, Windows and container distributions remain separately signed.
Linux requires glibc 2.39+. Wine validation does not establish native Windows hardware
coverage. Browser Host uses its own version; firmware retains the published 0.11.0
Store capability snapshot.

The timer recipe uses tighter spacing to fit its title and complete controls inside
round-screen safe areas. Debugging guidance checks partially clipped content and
separates ordinary device geometry from rectangular Store captures.

The reaction recipe provides a complete 30-second target game with touch/controller
input, pause/resume, win/loss and restart. Generated winning and timeout scenarios
exercise the actual Guest; final builds omit the accelerated timeout macro.

Workspace application packaging explicitly selects Release to match the standalone
DevKit defaults, including optimization and assertion settings. Incremental caches
are reconfigured, and `PXA_KEEP_WASM_DEBUG=ON` still retains debug information.
