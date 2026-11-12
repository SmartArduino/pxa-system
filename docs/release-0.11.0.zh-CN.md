# PXA 0.11.0 与 Agent Kit 0.7.0

本版完善双核 Host 工作调度、图像处理、板级相机与 AI 创作和发布流程。
Guest Core 仍为 2.1，原生 Host C ABI 仍为 2；应用按实际声明的服务和特性协商兼容性。

## 渲染与设备

RGB565 图像在小端 Host 上直接使用原有字节顺序，避免加载时逐像素重写。
固件的光栅工作线程按调用核心选择另一核心，兼顾主循环和后台渲染调用。
Guest 的事件与主循环仍按原有执行模型运行；本版不把应用自动转换成共享内存多线程。

维护固件新增 doit-gamekit-2 接入，区分旧款 GameKit 的芯片、屏幕和接线。
P4 完善显示旋转、音频、触摸及原生 USB 调试。相机外部资源预算与板级 DMA
分别管理，采集与变换缓冲分开分配，降低录像后重新打开相机时连续内存不足的风险。
Pai 使用经过审查的 esp32-camera 2.1.9，保留完整行 SRAM DMA 采集；为原生
运行栈和 DMA 留出内存，PXADB 解析缓冲移至 PSRAM，并提供控制任务栈余量查询。
共享 I2C 适配修复接收状态残留和探测操作生命周期；不会修改开发机的 ESP-IDF 源码。

## AI 创作与商城

计时器和计数器配方继承项目锁定 SDK 的屏幕配置，避免生成该 SDK 不支持的测试场景。
桌面 Debug AOT 使用较低优化级别缩短修改后的编译等待；设备及正式发布优化选项不变。
创作流程继续要求真实构建、实际操作、语义断言与原始截图检查。

首次浏览器登录后可记住此电脑的发布账号，后续应用通过各自签名证明取得短期上传权限。
商城构建自动补充独立 Wasm 包，并分别打包所选原生目标；各变体保持同一身份、版本
和发布序号。整组提交前检查包摘要和构建输入，部分失败时报告尚未完成的目标。
已上架应用修改资料时保留获批页面，待资料草稿审核通过再替换；提交成功仍需审核。

## 分发与升级

DevKit 提供签名的 Linux x64、Windows x64 和容器分发，Agent Kit 独立编号为 0.7.0。
Linux 原生包要求 glibc 2.39+；Windows 包注明实际验证范围，Wine 不能替代原生 Windows
窗口、音频与串口验收。设备应核对真实 Host 能力，固件发行号不是应用兼容性的唯一依据。
完整固件用于首次安装并重写数据分区；已有设备核对分区表后使用保留数据升级。

## English

PXA 0.11.0 improves native worker scheduling across cores and avoids redundant
RGB565 normalization on little-endian Hosts. Guest Core remains 2.1 and native
Host C ABI remains 2. Guest event/main-loop execution is unchanged; applications
are not automatically converted to shared-memory multithreading.

Maintained firmware adds GameKit 2 support and improves P4 display, audio, touch,
USB debugging and camera allocation. P4 capture and transform buffers are
allocated separately to tolerate fragmentation after recording. Pai retains
whole-row SRAM DMA with reviewed esp32-camera 2.1.9 and reserves internal memory
for camera DMA and native stacks. A shared I2C overlay fixes stale receive state
and probe-operation lifetime without modifying the installed ESP-IDF checkout.

Agent Kit 0.7.0 uses the locked SDK's test profiles, speeds desktop Debug AOT
iteration, remembers browser-approved publisher accounts, and builds separate
native and mandatory Wasm Store packages. Published listing content remains
visible while edits await review. Builds, runtime checks and visual inspection
remain necessary; a successful submission still requires Store approval.

Distributions retain signed manifests and independent SDK/AI/app versioning.
Linux x64 needs glibc 2.39+. Windows artifacts state their actual validation
scope; Wine does not establish native Windows hardware validation. Preserve
device data with an application-only update after checking the partition map.
