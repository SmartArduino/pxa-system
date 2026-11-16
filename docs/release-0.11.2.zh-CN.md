# PXA System / DevKit 0.11.2 与 Agent Kit 0.7.5

Assets 服务更新为 2.7，新增可选的图片编码输入预算查询。C 与 C++ SDK 提供对应
类型接口，返回后端最大输入、当前建议输入和动画上限；调用方指定输出像素预算。
查询仅提供当前建议，不保留内存，实际解码继续检查应用、共享池与临时预算。

支持的新静态图片输入校验上限为 16 MiB；默认输入预算仍为 128 KiB，默认像素
预算不变。GIF 动画输入最多 512 KiB。旧 Host 不支持查询时，应用应回退到其
原有 512 KiB 上限，并继续处理预算不足、取消和不支持的编码。

完整 DevKit 包含对应 Guest SDK、工具链、原生模拟器与安装器。Agent Kit 0.7.5
匹配 DevKit 0.11.2，更新图片预算开发指引与能力检查上下文。SDK 版本、固件版本
和服务版本各自独立：官方固件为 0.11.4，Browser Host 为 0.11.3，
Guest Core 仍为 2.1，Host C ABI 仍为 2。

Windows 原生模拟器为 AOT 代码和数据遵守低地址映射请求，避免 32 位绝对地址
重定位失败；页保护调用提供有效的旧保护输出并遵循统一的返回值约定。
通过 `--launch` 启动的 Guest 加载或运行失败时，桌面模拟器返回失败状态，
使 `pxa run` 能报告错误，避免把退回桌面误认为应用验收通过。

浏览器模拟器与原生 Host 的能力声明按各自后端实现生成；能力位不保证当前剩余
内存。旧商城能力快照保持不变，新版使用单独的 0.11.2 快照。

现有产品集成通过带签名锁文件的 Host SDK 0.11.2+idf.1 更新，并保留自身外设、
分区与数据策略。字体、应用和私有存档不应在 SDK 升级时被替换为空目录。

English: Assets 2.7 adds optional live image-input budget queries with typed
C/C++ APIs. DevKit 0.11.2 and Agent Kit 0.7.5 keep Core 2.1, Host C ABI 2 and
the existing AOT ABI. Use the returned current budget, retain older-Host
fallbacks and handle allocation failure; queries do not reserve memory.
