# Reference System UI

This directory owns the standard implementations of replaceable system roles:
Home, Settings, status/navigation bars, lock screen, permission prompt, package
installer and app manager.

`reference_layout` is backend-neutral and computes safe, responsive regions
from the core display profile. `reference_lvgl` is the first view adapter. A
different renderer can reuse the layout and system contracts without linking
LVGL.

Home and Settings are ordinary native applications. They register canonical
identities and replaceable system roles; launching native or PXA applications
always goes through the same task manager and Intent protocol.

The reference feature mask lets constrained products omit Home, Settings,
status bar, navigation bar, or the Home wallpaper. Products may also replace
either role with a higher-priority native or PXA implementation.

The reference Home uses a responsive phone-style icon grid over an adaptive
theme-colored wallpaper. Set `PXSYS_REFERENCE_UI_ENABLE_WALLPAPER=0` to remove
wallpaper rendering from constrained builds, or clear
`PXSYS_REFERENCE_UI_WALLPAPER` from the runtime feature mask without rebuilding.
Products can set `pxsys_reference_lvgl_config_t.wallpaper_source` to a borrowed
LVGL image source; it is center-cropped with cover sizing for the active display
profile. A null source keeps the allocation-free built-in wallpaper.

Launcher icons are resolved by the optional
`pxsys_reference_lvgl_config_t.resolve_app_icon` hook. The platform can return
an icon from a native resource table or an installed PXA package together with
its release callback. The LVGL adapter owns that reference only while the
launcher is using it. If no resource is available, the standard UI selects a
distinct semantic symbol from the canonical application id. Icon storage and
decoding therefore remain outside the backend-neutral application registry.

The optional `content_insets_changed` callback publishes the currently visible
status and button-navigation reservations to compatibility Native surfaces.
Gesture navigation reports no bottom reservation, so applications recover that
space without learning reference-UI geometry or linking its layout module.

Reference applications use ordinary canonical application identities and the
same Intent/RPC/event protocol as third-party native and PXA applications.
Nothing in the core gives a reference application a private navigation path.

## System input method

`reference_ime` is the nine key input method used on narrow panels. It binds to
any focused text input the system UI can see, including the text inputs a PXA
Guest creates through the UI service, and reports every edit through the
ordinary LVGL textarea, so the Guest receives them as `PXA_UI_EVENT_TEXT`
events.

* narrow panels (less than 480 logical pixels) use the keypad: pinyin with
  candidates, English multi tap with candidates and case switching, a number
  page and Chinese/English symbol pages, plus the `中`/`EN`/`#+`/`收起` keys;
* wide panels use the full keyboard with the pinyin input method behind it;
* closing the keypad submits the text (`LV_EVENT_READY`), so a Guest that waits
  for the submitted event applies the query the user typed;
* the Wi-Fi dialog keeps its own keyboard, and the input method never opens for
  an input the user has not focused.

Enabling it needs LVGL's pinyin input method: `LV_USE_IME_PINYIN` with
`LV_IME_PINYIN_USE_K9_MODE` (Kconfig `CONFIG_LV_USE_IME_PINYIN` and
`CONFIG_LV_IME_PINYIN_USE_K9_MODE` in an ESP-IDF build).
