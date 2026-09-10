# 应用与协议模型

[English](../app-protocol.md)

## 1. 应用身份

规范身份是 `<publisher-root>:<app-id>`。publisher root 来自已验证签名链，不能
由 Guest 声明；runtime 与 artifact 类型不属于身份。Component ID 只在应用内
命名执行单元。

## 2. Manifest

Manifest 声明 SDK 范围、components、artifacts、服务、权限、Intent filter、
角色请求和资源。安装器必须在目录提交前完成格式、签名、兼容性和资源边界校验。

## 3. 生命周期

生命周期按 `discovered -> resolved -> starting -> running -> stopping -> stopped`
推进，失败进入 fault。异步 start/stop 必须保留实例所有权到确认完成，不能仅因
页面不可见就销毁应用身份或私有数据。

## 4. Intent 与导航

Intent 描述 action、目标、URI、MIME、flags 和 bounded extras。解析器依据完整
身份、filter、角色和产品策略选择目标。Back、Home、角色启动和跨 runtime 导航
进入同一个 Task/Intent 通道；wire record 使用明确 magic、版本和长度。

## 5. 跨应用通信

RPC、topic event 和 service endpoint 都由 Host 注入 caller principal。Guest
不能伪造调用者。可靠事件必须有界并定义背压；大数据使用 handle/stream。

## 6. 服务命名

标准服务使用 `system.*`，厂商扩展使用反向域名，例如
`com.example.sensor.air-quality`。发现按接口 ID、语义版本和 feature bits 协商。

## 7. 系统角色与主题

角色注册只接受产品授权候选。前台角色进入 Task Manager；状态栏等常驻角色由
Role Host 管理。普通 UI 必须使用 background、surface、text、muted、primary、
success、warning、danger 等语义 token，并响应 environment generation。

## 8. 错误、异步工作与安装

状态码区分无效输入、未授权、不支持、资源不足、忙、取消和内部错误。异步工作
由 request token 关联，completion 回到 owner thread。安装使用可恢复 slot 事务；
签名复验、身份锁、fsync 与原子目录发布必须先于可启动状态。
