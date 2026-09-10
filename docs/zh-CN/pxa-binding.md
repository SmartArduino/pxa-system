# PXA Binding

[English](../pxa-binding.md)

## 1. Service envelope

Guest 通过 `pxa_control` 和 `pxa_io` 发送有界二进制 envelope。每条记录包含
service、opcode、request ID、flags 与 payload 长度；decoder 在读取前校验边界、
版本、枚举和保留位。多字节整数使用规范字节序，禁止直接映射宿主结构体。

## 2. Caller identity

Host 根据已验证 package、运行中的 app/component 和 lifecycle generation 构造
principal。来自 Guest payload 的身份字段永远不能作为授权依据。

## 3. 线程

WAMR worker 只执行 Guest。影响系统、UI 或产品 service 的操作投递到系统 owner
thread；completion 再排队回 Guest。Host 不能在 Guest callback 中同步重入另一
Guest，也不能让后端线程直接修改 Core。

## 4. 演进

协议以 major/minor 与 feature bits 协商。新增字段必须可跳过或由记录长度保护；
不兼容语义使用新 major。Native 与 PXA binding 必须通过相同的 golden vector、
身份和授权测试。
