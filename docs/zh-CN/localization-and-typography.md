# 本地化与字体

[English](../localization-and-typography.md)

## Locale service

`pxsys_locale_service_t` 保存规范 BCP 47 locale、文字方向和 generation。标签会
规范化，例如 `ZH-hans-cn` 变为 `zh-Hans-CN`。产品在标准系统配置中设置初始值，
运行时通过 service 原子更新；Native、PXA 与参考 UI 观察同一快照。

## Resource catalog

不可变 catalog 按字符串 namespace 挂载。解析顺序是高优先级、最具体 locale、
父 locale、语言无关 fallback。`system.*` 由系统保留，产品可以更高优先级覆盖
资源而不 fork 实现。catalog 内存由调用者持有，可直接放在 flash。

## 应用元数据

规范身份与翻译后的 name、description、icon 分离。descriptor 提供默认值和可选
资源 key；每个字段独立 fallback。PXA package 的 locale YAML 可在 `metadata`
中提供本地化启动器信息，`package.json` 仍是最终 fallback。翻译、图标和 runtime
类型都不能改变应用身份、权限、存储 key 或 Intent target。

## 翻译编写

每个应用使用一份 `i18n/messages.yaml` 和每语言一份 block-style YAML：

```text
i18n/
  messages.yaml
  zh-CN.yaml
```

key 描述语义而不是复制源文本；源条目包含 context、长度和命名 placeholder 类型。
禁止拼接翻译句子。`tools/i18n/compile_catalog.py` 检查重复/未知 key、locale、
placeholder 与字节上限，并生成 `pxa_app_messages.h`。locale 变化后应用更新
`pxa_i18n_t` 并重绘当前 view。

## 语义字体

主题定义 `DISPLAY`、`HEADLINE`、`TITLE`、`BODY`、`LABEL`、`CAPTION` 六个角色。
renderer 将角色映射到具体字体，可根据 locale 选择 CJK、Arabic 等字形。图标使用
独立字体角色；字体文件和 glyph subset 属于产品资源，不进入 Core ABI。
