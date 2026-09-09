# Localization and typography

PXA System keeps language, resources, and typography above the rendering
backend. LVGL is one consumer of these contracts; it is not part of their
public meaning.

## Locale service

`pxsys_locale_service_t` owns the current canonical BCP 47 locale and text
direction. Tags are normalized when snapshots are created, for example
`ZH-hans-cn` becomes `zh-Hans-CN`. Locale observers receive the current value
immediately and are notified again after a runtime language change.

Products set `pxsys_standard_system_config_t.initial_locale` before creating
the standard system. They can change the locale later with
`pxsys_locale_service_update()`. The simulator accepts `--locale zh-CN`, and
its automation protocol accepts `locale zh-CN` for runtime switching.

## Resource catalogs

`pxsys_resource_service_t` mounts immutable catalogs under a string namespace.
Native code and runtime bridges use the same namespace and resource keys; the
resource name does not depend on whether its owner is Native or PXA.

Resolution follows these rules:

1. Higher catalog priority wins, allowing a product package to replace a
   standard implementation without forking it.
2. Within one priority, the most specific locale wins.
3. Locale lookup falls back through parent BCP 47 tags, such as
   `zh-Hans-CN` to `zh-Hans` to `zh`.
4. An empty catalog locale is the final language-independent fallback.

`system.*` namespaces are reserved for system packages. The reference UI uses
`system.ui` at priority `-100`, leaving positive priorities available for
product overrides. Catalog memory is borrowed, so compiled entries must remain
alive until they are unregistered. Embedded products can therefore mount
catalogs directly from flash without copying every translated string to RAM.

## Semantic typography

Themes define requested sizes for six semantic text roles:

| Role | Default | Intended use |
| --- | ---: | --- |
| `DISPLAY` | 28 px | Time and short high-emphasis values |
| `HEADLINE` | 24 px | Page headings |
| `TITLE` | 20 px | Card and dialog titles |
| `BODY` | 16 px | Primary content |
| `LABEL` | 14 px | Controls and compact metadata |
| `CAPTION` | 12 px | Secondary metadata |

The values live in `pxsys_theme_snapshot_t.typography_px`. A backend maps each
role to a concrete font face and may choose the closest available size on a
constrained target. The reference LVGL configuration accepts one font per
role and an optional resolver receiving the role, requested size, and current
locale. It falls back to its legacy body/title pair when a product only ships
one or two fonts.

The PXA UI wire protocol preserves its original role numbers and adds `LABEL`,
`HEADLINE`, and `DISPLAY`. Hosts without dedicated faces render the extended
roles through their existing text fallbacks. Icons remain a separate role and
font family.

Font selection should be script-aware. A product font provider may return a
CJK, Arabic, Devanagari, or Latin face for the same semantic role without
changing application layout code. Font files and glyph subsets remain product
assets rather than part of the core ABI.
