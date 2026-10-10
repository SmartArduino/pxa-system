# SDK Patterns

使用 C++26 SDK 的小型完整示例：带版本的类型化存档、最多 2 B 一次的文件读写、
可重试关闭，以及 App::on_error 处理未处理的异步任务错误。界面随 Host DPI 缩放，
使用形状和安全边距计算 padding，没有自绘按钮或独立命中表。

点击 Add and save 保存计数；退出重启会读回。File round trip 写入、关闭、重新
打开并严格解码。Fail task 触发真实文件 not_found，显示 Task error handled，
随后仍可执行其他按钮。完整接口说明与模拟器验证见
[API_POLISH.zh-CN.md](../../API_POLISH.zh-CN.md)。
