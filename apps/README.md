# Reference Applications

[简体中文](README.zh-CN.md)

`apps/pxa/` contains the reference PXA application set shipped with this
repository. These applications exercise the public Guest SDK, package metadata,
localization, semantic themes, services, Canvas, and multi-Component runtime.

Application chrome, forms, status text and ordinary controls use semantic UI
theme tokens. Canvas and raw surfaces are reserved for games, visualization and
media; their content palette may be application-owned, while surrounding system
chrome remains controlled by PXA System.

The package tools use `apps/pxa` by default. Products may select a subset of
these applications or point `PXA_APP_SOURCE_ROOT` at another source tree; the
portable system libraries never require a particular application set.

The key under `apps/pxa/.dev-signing` is an explicitly non-production test
fixture. Products must supply their own protected signing process and trust
policy.

Run `python3 tools/apps/check_ui.py apps/pxa` to verify that every reference app
has complete source/Chinese catalogs and locale lifecycle handling, and that
ordinary UI entry points use semantic theme tokens.
