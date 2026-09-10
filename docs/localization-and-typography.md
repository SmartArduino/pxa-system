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

The standard Settings application includes a scrollable language picker. Its
LVGL adapter ships an English/Simplified Chinese list by default, while
`pxsys_reference_lvgl_config_t.languages` accepts a product-owned list of
canonical BCP 47 tags and native display names. The list is borrowed for the UI
lifetime so embedded products can keep it in flash. Selecting an item only
updates the backend-neutral locale service; every native or PXA consumer sees
the same locale generation.

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

## Application metadata

`pxsys_app_descriptor_t` keeps application identity separate from translated
presentation. `display_name`, `description`, and `icon_reference` are default
fallbacks. An App may also declare one resource namespace and independent keys
for those three fields. `pxsys_app_metadata_resolve()` resolves each field for
the current locale and falls back to its descriptor default independently, so a
missing description translation does not discard a translated name or icon.
`pxsys_app_metadata_t.catalog_fields` identifies fields supplied by a catalog;
an empty-locale default catalog is intentionally included in that result.

A Native App can use the contract without depending on libpxa:

```c
pxsys_app_descriptor_t app = {0};
app.struct_size = sizeof(app);
app.display_name = pxsys_string_from_cstr("Weather");
app.description = pxsys_string_from_cstr("Weather near you");
app.icon_reference = pxsys_string_from_cstr("assets/weather.png");
app.resource_namespace = pxsys_string_from_cstr("vendor.weather");
app.display_name_resource_key = pxsys_string_from_cstr("metadata.name");
app.description_resource_key =
    pxsys_string_from_cstr("metadata.description");
app.icon_resource_key = pxsys_string_from_cstr("metadata.icon");
```

The metadata contract is shared by Native and PXA Apps. A Native App can mount
the keys in the resource service. A package host maps signed manifest metadata
to the same `app.<app-id>` namespace, standard keys, and
`pxsys_app_metadata_t` result through the platform metadata source. The
reference UI does not branch on runtime type. Platform bridges cache the
resolved package metadata and refresh it only when the package catalog or
locale changes, so normal UI redraws do not parse manifests or perform package
I/O. Application identity, intent targets, permissions, private storage, and
task identity never contain a localized name.

The descriptor extension is ABI-additive. Registries accept the original v1
prefix and store a current full descriptor, leaving existing Native Apps usable
without the PXA SDK. Manifest 0.6 carries signed localization records so the
launcher and installer can resolve metadata without starting App code.

A PXA App declares its language-independent defaults directly in
`package.json`. This is sufficient for an App that does not need i18n:

```json
{
  "id": "pxa-weather",
  "version": "0.1.1",
  "name": "Weather",
  "description": "Local weather conditions",
  "icon": "assets/icon.png"
}
```

When an App needs localized presentation, each locale YAML may add an optional
`metadata` mapping alongside its in-App string translations:

```yaml
locale: zh-CN
metadata:
  name: 天气
  description: 当地天气实况
translations:
  screen.title: 天气
```

The `metadata` mapping and `translations` mapping are independently optional.
An App may localize only its launcher metadata, only its in-App content, both,
or neither. The packager uses `package.json` as the final fallback and emits
locale metadata as Manifest 0.6 records. The legacy
`package.json.localizations` input remains readable for compatibility, but it
cannot be mixed with YAML metadata and is not used by standard Apps.
The package defaults should normally use the same source language as
`messages.yaml.default_locale`.

An App with no localization omits the `i18n` directory entirely. A metadata-only
localized App keeps only the catalog namespace and default locale:

```yaml
namespace: app.quick-note
default_locale: en-US
```

Its locale files may then contain `metadata` without `translations`; those
values are compiled into the manifest and do not add a runtime string catalog.

Locale tags must already be in canonical form. Each locale may override any
non-empty subset of `name`, `description`, and `icon`; omitted fields fall back
independently. Localized icons are inventoried and signed like every other
package file.

Every installable App must have a non-empty default display name. Production
package validation should also require a default icon when a product UI cannot
provide a generic one. Unsupported locales do not prevent installation or
launch: resolution uses the locale parent chain, then an empty-locale catalog,
then the descriptor fallback. The UI must never display a blank name merely
because a translation is absent.

Localized icons are allowed for graphics containing language- or culture-
specific content, but normal App icons should be language-neutral. Locale,
theme, density, and display-shape variants are orthogonal selection dimensions;
they must not change the App identity or resource key.

## Translation authoring

Resource keys describe meaning and context rather than copying source text.
For example, `file.action.open`, `door.state.open`, and
`store.status.open` remain separate even if their English source happens to be
the same. Translators need a source-language value plus context, placeholder
definitions, length guidance, and an optional screenshot reference.

Do not construct sentences by concatenating translated fragments. Messages
with dates, numbers, or named values use named, typed placeholders. Quantities,
gender, and other selection rules that cannot yet be represented by the
formatter use separate complete-sentence keys for every semantic case.

Each App owns one semantic source file and one ordinary, block-style YAML file
per translated locale. The package tool compiles these files into a generated
`pxa_app_messages.h` before compiling App sources:

```text
i18n/
  messages.yaml       # keys, source text, context, placeholders, limits
  zh-CN.yaml
  zh-Hant.yaml
```

`messages.yaml` is the source-language catalog. It uses semantic keys rather
than English text as identifiers:

```yaml
namespace: app.weather
default_locale: en-US
messages:
  update.time:
    source: Updated {time}
    context: Last successful weather update time
    max_bytes: 64
    placeholders:
      time: string
  action.refresh:
    source: Refresh
    context: Button that requests current weather
    max_bytes: 32
```

A translated catalog contains only the locale and its translations. Missing
entries are allowed and fall back independently to the source catalog:

```yaml
locale: zh-CN
translations:
  update.time: 更新于 {time}
  action.refresh: 刷新
```

The authoring format deliberately uses YAML mappings only. Flow-style JSON
objects and arrays are not part of the catalog format. Indentation is two
spaces, message and placeholder keys are unique, and locale tags use canonical
BCP 47 casing. Quoted YAML strings remain available when a value contains a
colon or leading/trailing whitespace.

Business code never selects a language and never stores parallel translated
strings. It references the generated message identifier instead:

```c
pxa_i18n_t i18n;
pxa_i18n_init_from_start_config(&i18n, &pxa_app_i18n_bundle,
                                config, config_length);

const char *label = pxa_i18n_cstr(&i18n, PXA_MSG_ACTION_REFRESH);
```

Named placeholders are formatted without relying on their order in the
sentence, allowing a translation to reorder them:

```c
const pxa_i18n_argument_t arguments[] = {{
    .name = "time",
    .name_size = 4,
    .type = PXA_I18N_ARGUMENT_STRING,
    .value.string = {.data = update_time, .size = update_time_size},
}};
pxa_i18n_format(&i18n, PXA_MSG_UPDATE_TIME, arguments, 1,
                output, sizeof(output));
```

The generated header stores immutable catalogs suitable for flash and assigns
compact numeric IDs to messages. Locale changes arrive through the standard
system configuration event; an App updates its `pxa_i18n_t` and rerenders the
current view. Lookup tries the most specific matching catalog and then the
default source text for each missing message.

Per-locale files reduce merge conflicts. Comparison tables and XLIFF exports
should be generated review artifacts rather than sources of truth. The catalog
compiler currently rejects unknown keys, duplicate keys, invalid locale tags,
placeholder mismatches, invalid placeholder types, and strings that exceed
`max_bytes`. Pseudolocale expansion and screenshot overflow checks remain UI
validation tasks.

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
