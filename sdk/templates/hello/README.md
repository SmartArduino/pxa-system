# 起点与验收场景

这是可直接运行的计数器起点：主题背景铺满屏幕，内容使用 SafeArea，按钮默认垂直居中。
修改应用时保留身份和 SDK 锁，按作品需求替换页面及测试，不把计数器测试当作新作品的验收。

安装 Agent Kit 的工作区可运行（替换应用目录）：

```sh
.pxa/pxa verify apps/counter --profiles pai-touch generic --scenario apps/counter/tests/basic.json --require-ui --jobs 2
.pxa/pxa verify apps/counter --profiles pai-touch generic --scenario apps/counter/tests/basic.json --require-ui --jobs 2 --no-build
```

第一条只构建一次，再并行运行各屏幕；第二条在输入、参数、SDK、签名身份和包摘要匹配时复用该包。
`--no-build` 使用准备好的 Debug 或 Release 包，调试接口可独立开启；有测试宏时须重复相同的 `--define`。
源码、资源、依赖或应用目录输入变化后重新构建，不删除 `pxa.lock`。验证使用独立临时存档，保留日常模拟器数据。

`tests/basic.json` 断言初始值为 0，按文字定位 +1 按钮，等待值为 1，并保存真实截图。
添加番茄钟、列表或游戏行为后同步改成自己的开始、暂停、重置、保存或结束场景。
没有普通保留式 UI 的 Surface 游戏不使用 `--require-ui`，应检查实际画面和输入结果。
`result.json` 和原始截图用于核对；contact-sheet.png 仅是检查预览，不能作为商城截图上传。
