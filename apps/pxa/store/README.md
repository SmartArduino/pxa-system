# PXA App Store

A reference PXA application that browses the DOIT PXA device catalog
(`https://app.doit.am`), shows each application's signed metadata,
compatibility, permissions and service requirements, and reports what an
installation would need.

## Building

```sh
tools/app.sh build store --target simulator --source-root deps/pxa-system/apps/pxa
tools/app.sh build store --target esp32s3 --source-root deps/pxa-system/apps/pxa
```

## Configuration

| Define | Default | Notes |
| --- | --- | --- |
| `PXA_STORE_ORIGIN` | `https://app.doit.am` | Must stay byte-identical to the signed `net.client` scope in `package.json`; the Host compares the parsed URL origin with that scope. |
| `PXA_STORE_PROFILE` | `esp32-s3-wamr-2.4.0` | Device profile requested from the catalog. |
| `PXA_STORE_CHANNEL` | `stable` | `stable` or `beta`. |

Override them with `PXA_APP_DEFINES`, for example
`PXA_APP_DEFINES=PXA_STORE_ORIGIN=https://store.example.test`. Changing the
origin also requires updating the `net.client` scope in `package.json`, which is
part of the signed manifest.

## Layout

The screen follows the Host-reported logical size, the safe area and the system
bar insets:

- `safe_insets` from the UI environment (start configuration and
  `PXA_UI_ENVIRONMENT_CHANGED`) cover the panel's own safe area: rounded
  corners, cutouts and any reserved region the product reports.
- `system-bar-insets` from the Window service snapshot cover the status and
  navigation bars. The App requests `get-snapshot` and also consumes
  `metrics-changed`, then pads by the larger value of both per edge. Nothing is
  hardcoded: a Host that reports no bars simply gets no extra padding.
- Layout metrics (header, chips, cards, tab bar, keyboard) switch between
  compact, regular and large tables from the reported width, so the same App
  stays usable on a 296x240 panel and on a phone-sized display.

The search screen uses a real text input (`PXA_UI_CONTROL_TEXT_INPUT`) and
subscribes to `PXA_UI_EVENT_TEXT`. It draws no keyboard of its own: the system
input method binds to the focused input, shows the nine key pad with pinyin
candidates, English multi tap and symbol pages on narrow panels, and the full
keyboard with pinyin on wide panels. The system reports each edit as a text
event, so the query follows the system keyboard, and the input's submitted
event (its confirm key) applies the search.

Safe area insets and the layout's own margins do not stack: the root only adds
the part of an inset a screen margin does not already cover (`inset_padding`),
so a gesture strip never pushes the content further than the strip itself.

Scrolling down hides the header, the filter row and the bottom tab bar. They
return on the first upward scroll, and also on any drag inside the list, which
covers the case where the taller list no longer scrolls. Hiding is a small
`PATCH` transaction toggling `visible` on those nodes, so the surface is not
rebuilt per scroll event.

Wide displays (`>= 480` logical pixels, `>= 720` for three columns) render the
catalog as a grid with fractional, equally sized columns through the UI grid
ABI. Narrow panels keep the single-column list.

The catalog loads incrementally: scrolling past the previous mark requests the
next page with no explicit button. The loaded entries live in a bounded ring
window of `STORE_MAX_APPS` records, so the oldest entry is overwritten once the
window is full and Guest memory stays flat no matter how large the catalog
grows.

## Backend contract

The App reads the signed device API and skips envelope signature verification:

```text
GET /api/v2/catalog?profile=...&channel=stable&view=compact&page_size=2
    [&kind=game|app][&category=SLUG][&cursor=N][&device_id=MAC][&q=QUERY]
GET /api/v2/apps/{app_id}?profile=...&channel=stable&view=compact[&device_id=MAC]
```

`kind`/`category` filter the catalog server-side; the first compact page also
carries the signed `taxonomy` (kinds and categories with Chinese and English
labels) that the App renders as its bottom tabs and category chips.

`view=compact` is required because the Host Network service bounds one response
at 4096 bytes, while a full catalog item is about 4.3 KiB: it embeds the Host
capability declaration, the publisher SPKI, all three digests and the full
compatibility detail. The compact catalog entry drops the repeated Host
declaration, the SPKI, the container/manifest digests, the download ticket URL
and the changelog, truncates free text, and caps the permission list, so two
entries fit one page. The compact application detail keeps the full
compatibility, permission, service and artifact detail for one entry.

Measured against the production catalog (`pxa-voxel-craft`):

| Response | Bytes |
| --- | ---: |
| `GET /api/v2/catalog` (full, `page_size=1`) | 4578 (over the 4096 cap) |
| `GET /api/v2/catalog?view=compact&page_size=2` | 2152 |
| `GET /api/v2/apps/{id}?view=compact` | 2927 |

The device identity (`device.identity` / `mac.wifi.station.hardware`) is only
used for `device_id` rollout bucketing; when it is unavailable the App still
loads the catalog.

## Installation

The installation panel states the two platform limits explicitly:

- the Guest SDK publishes no package-installer service, so an App cannot hand a
  downloaded container to the Host installer; installation stays with the
  product App Manager or the inbox;
- the Network service caps one response at 4096 bytes, so a `.pxa` container
  (hundreds of KiB) cannot be downloaded through it.

Pressing **Install** repeats that the current firmware cannot install from the
Guest. The panel is driven by the artifact size reported by the store, not by a
hardcoded capability flag, so it stays accurate when the platform gains the
missing services.
