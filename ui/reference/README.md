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

`PXSYS_REFERENCE_UI_BUILTIN_SETTINGS=0` removes the built-in Sound, About, Apps,
Files and Wi-Fi detail pages. Home, the Settings landing page, appearance,
language, keyboard preferences and status/navigation roles remain independent.
Network rows use the product's network-settings role when the built-in details
are unavailable. The Wi-Fi and cellular compile switches are independent of
this setting; battery percentage also works with both radios disabled.

The runtime `features` mask controls registration of individual roles. Clearing
Home still permits `pxsys_reference_lvgl_start()` to start enabled chrome roles;
calling `pxsys_reference_lvgl_home()` explicitly then returns `NOT_FOUND`.
Card recents fall back to a list when LVGL snapshots are disabled. Disabling
pinyin or K9 mode retains the full keyboard fallback.
Native button integrations use `pxsys_reference_lvgl_show_recents()` and
`pxsys_reference_lvgl_home()`. Both retain the leaving application's preview;
the recents entry captures before its overlay and animation cover the app.

Native test builds compile all four radio combinations with built-in settings
enabled and disabled. Feature tests exercise all 16 combinations of Home,
Settings and the two bars, including repeated startup/teardown, theme updates,
recents and keyboard fallback. These test targets add no product runtime cost.

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

In fullscreen applications, a downward swipe starting at the hidden status
bar's physical top edge temporarily reveals the status bar. The first gesture
is consumed through release; a second swipe from the visible bar opens the
notification shade. A tap, short swipe or cancelled gesture does neither.
Hidden status chrome overlays the application without changing its content
insets or revealing hidden navigation. The existing 2.5-second transient timer
hides it again, deferring expiry while a chrome gesture or shade is active.
Window-policy changes clear the temporary reveal. Transient status bars keep
the same reveal-first policy; permanently visible bars open the shade directly.

Reference applications use ordinary canonical application identities and the
same Intent/RPC/event protocol as third-party native and PXA applications.
Nothing in the core gives a reference application a private navigation path.

Gamepad products use `pxsys_reference_lvgl_handle_gamepad()` on every input
poll, before shortcuts, keypad navigation or application controller delivery.
The lock screen has no focus ring. Any fresh gamepad button press unlocks it;
the entire sequence, including added buttons and its final release, is consumed
so it cannot activate the resumed UI or game. Products first consume a sleeping
screen's wake press and its full release, then call this policy for subsequent
input; waking and unlocking require separate presses. A sequence already held
when locking must first be released.
When the function returns true, products clear keypad focus, hide their fallback
cursor and suppress physical and injected controller reports. Pass the combined
buttons from both input sources, including zero reports.

Launcher focus selects the icon's current page before scrolling, so page snap
and keyboard navigation stay in sync after icon reordering. Recent-task cards
and list rows accept `LV_KEY_DEL` to close the focused task; GameKit maps native
UI X presses to this key. Game-owned X input bypasses the keypad adapter.

Settings and other scrolling system pages extend their viewport to the system
display safe-area edge when there is no navigation bar. They reserve no height
for a disabled navigation role. A product's bottom safe inset therefore controls
the blank screen edge; a zero inset uses the full height. A visible button bar
keeps its actual reservation, and gesture handles retain their scroll padding.

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
