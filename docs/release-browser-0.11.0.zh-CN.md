# Browser Host 0.11.0

本版用 PXA 0.11.0 重建浏览器 Host，Guest Core 仍为2.1。兼容清单与实际
服务注册表同步，保留浏览器可用的服务子集；未实现的硬件或原生网络能力仍返回
不支持。RGB565图像加载跳过小端主机上的重复像素转换。

浏览器运行时与设备固件、DevKit和应用分别编号，发布至独立的
`browser-v0.11.0`。网站与商城使用校验摘要的固定版本目录，保留历史运行时，
避免已打开的页面混用不同版本的资源。主循环应用仍需浏览器支持 WebAssembly JSPI。
模拟器和网页结果不能替代设备的内存、帧率、摄像头及物理输入验收。

## English

Browser Host 0.11.0 rebuilds against PXA 0.11.0 with Guest Core 2.1. Its capability
snapshot follows the compiled service registry while retaining the browser's
implemented subset. RGB565 loading avoids redundant little-endian normalization.

The independent browser-v0.11.0 release uses immutable, hash-checked runtime
directories. Preserve older directories for existing pages. Main-loop apps still
require WebAssembly JSPI; browser results do not establish device performance.
