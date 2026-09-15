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

A direct-build application that retains GuestMapped Surface buffers declares a
bounded, pinned linear memory in `package.json`:

```json
"build": {
  "system": "direct",
  "linear_memory": {"maximum_bytes": 2097152, "pinned": true}
}
```

`maximum_bytes` is a 64 KiB WebAssembly page multiple. The packager writes and
verifies that maximum in every component module, then records `pinned: true` in
each Component's signed manifest entry. A supporting WAMR Host reserves that
Component's complete range only while instantiating it, so `memory.grow` cannot
relocate registered pixels. This does not make mapping portable to a Host that
reports GuestMapped as unsupported.

The key under `apps/pxa/.dev-signing` is an explicitly non-production test
fixture. Products must supply their own protected signing process and trust
policy.

Run `python3 tools/apps/check_ui.py apps/pxa` to verify that every reference app
has complete source/Chinese catalogs and locale lifecycle handling, and that
ordinary UI entry points use semantic theme tokens.
