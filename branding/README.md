# PXA 正式标识 v1

`pxa-logo-master.svg` 是 PXA 唯一的几何母版。图形由开放的像素窗口和相连的斜向模块构成；右上角的像素表示应用向外扩展。PXA 字标使用固定轮廓，不依赖字体，不加句点。系统、固件、Host、官网与商城共用本套标识，不为不同开发板重新设计 Logo。

## 颜色与版式

| 用途 | 色值 |
| --- | --- |
| 标准深色背景 | `#111827` |
| 深底字标和主图形 | `#e5e7eb` |
| 官网强调绿 | `#b6f568` |
| 深绿折面 | `#568144` |

深底用 `*-dark.svg`，浅底用 `*-light.svg`；单色输出选 `*-mono-*.svg`。横版用于网页页眉，竖版用于启动和品牌展示，小图标只用图形。图标最小 16 px，横版最小宽度 96 px；低于此尺寸使用图形，避免字标难以辨认。独立展示图形时四周至少保留图形宽度 1/8 的空白；网页横版和小图标使用导出的紧凑版，保留文件内置的留白。维持原比例，不旋转、描边、添加阴影或渐变，不重新打字替代字标。

## 统一导出

要求 Python 3、Pillow 和 `rsvg-convert`，仅在开发机生成产物，不链接进系统。默认输出的 `dist/` 是随版本维护的交付资产。SVG 不包含脚本、外部引用或字体。

```sh
python3 deps/pxa-system/branding/export.py \
  --firmware-header firmware/components/pxa_board_common/src/assets/pxa_boot_logo.h
```

向独立商城/官网仓库同步资源：追加 `--website-root /path/to/pxa_app_store_backend`。同步器复制到 `pxa-website/assets/brand/` 和 `app/static/brand/`，并更新两个 favicon。页眉引用横版 SVG，浏览器标签引用 favicon；不要单独手绘或修改导出副本。网站模板、CSS 与资源一起发布。

通用开机实现位于工作区 `pxa_board_common::BootScreen`，直接读取 Flash 中的 RGB565 常量，按屏幕比例完整居中，背景使用标准深色。启动提示由原生文本绘制，不烘焙进品牌图形。没有 SD 卡、字体文件或 Host 服务时也能显示；后续系统 UI 就绪后移除启动层。

导出文件与生成工具适用仓库的 MIT 许可证，版权声明见上级 `LICENSE`。依赖的许可证约定仍按系统许可政策执行。
