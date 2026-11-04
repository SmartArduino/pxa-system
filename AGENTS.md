# 系统许可证约定

PXA 系统及 Host 实现禁止使用 GPL/AGPL/LGPL/MPL 等 copyleft 实现。
允许经过逐项核实、不会要求系统开源的运行时例外。应用中的引擎不属于系统，
不得将其受限实现复制或改写后放入系统。

按 `docs/zh-CN/system-license-policy.md` 审查依赖及升级，保留原始版权与许可。
`tools/licensing/check_system_licenses.py` 是构建检查，不代替来源和二进制审查。
不得删除许可声明或盲目刷新审核哈希以通过检查。

# 商城截图

商城截图使用真实 PXA 模拟器运行应用后的原始画面，统一采用无圆角矩形视口。
拍摄前关闭圆角、圆屏遮罩及安全边距，关闭性能浮层并隐藏底部手势条和三按键导航。
使用 `verify --store-screenshots` 或 MCP `pxa_verify(store_screenshots=true)`，
同时关闭应用自身的性能浮层。上传前逐张检查原图，禁止裁剪、修图或生成图替代实际拍摄及上述设置。
