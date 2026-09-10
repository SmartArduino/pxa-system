# PXA 规范

[English](README.md)

`draft/` 是当前唯一草案目录。各 service 保留自己的真实协议版本，目录名本身
不是协议版本。`draft/pxa-*.json` 是字段、数值和版本的唯一机器真源，Markdown
负责解释语义，`golden/` 保存可复现的 Core、Package 与 UI 0.3 wire 示例。

```sh
python3 spec/draft/tools/test.py
python3 tools/package/test_ui_vectors.py
```

所有规范、golden 生成和校验工具均使用 Python，不依赖 Ruby。当前发布版本包括
Core 0.1、Window 0.1、UI 0.3、Network 0.2、Audio 0.2、Manifest 0.6，
其余草案 service 为 0.1。

完整文档地图见[当前草案中文入口](draft/README.zh-CN.md)。
