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

Reference applications use ordinary canonical application identities and the
same Intent/RPC/event protocol as third-party native and PXA applications.
Nothing in the core gives a reference application a private navigation path.
