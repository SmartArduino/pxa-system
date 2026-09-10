# 可移植性与扩展

[English](../portability.md)

## 1. Platform SPI

最小平台 SPI 包含分配器、单调/墙上时钟、跨线程唤醒、日志、持久配置、随机数和
签名校验。文件、网络、音频、传感器、输入和显示电源是可选 provider；缺失时
核心仍须可编译。

## 2. Renderer SPI

Renderer 实现 surface 生命周期、原子 UI transaction、资源、Canvas、输入、主题
安装、快照和诊断。`renderer_host` 拥有 provider，并在绑定和主题更新时传递完整
快照。当前参考后端为 headless 与 LVGL；稳定前仍需第二种图形后端验证中立性。

## 3. 后端专用 Native UI

Renderer 可提供受协商的 native extension。应用只能得到自己 surface 的 root，
不能访问 compositor 或其他应用对象；产品必须在解析阶段拒绝不兼容 renderer。

## 4. 设备与传感器

Provider 注册接口身份、版本、features、权限、并发、取消和清理规则。标准传感
模型使用厂商命名 descriptor，其他能力使用反向域名接口。Native 与 PXA consumer
最终进入同一 provider 和策略。

## 5. 产品 Profile 与测试

Profile 选择平台、renderer、runtime、容量、可信 publisher、角色、服务、默认
主题、预装应用和恢复实现。覆盖通过数据与注册完成，不 fork 核心 dispatch。
稳定合约必须覆盖 schema/golden vectors、两种 binding 等价性、生命周期与取消、
身份伪造、内存压力、provider 清理、模拟器、renderer 与主题切换。
