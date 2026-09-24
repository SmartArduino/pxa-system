# Display Profiles and Standard System UI

[简体中文](zh-CN/display-and-system-ui.md)

## Contract boundaries

`pxsys_display_profile_t` is the renderer-independent description of a logical
display. It carries resolution, density, refresh rate, safe insets, outer shape,
corner radii, and bounded cutouts. `pxsys_display_service_t` owns the current
snapshot and notifies UI implementations when a desktop window, fold state, or
hardware mode changes.

The core contract does not contain LVGL objects or SDL handles. A platform
adapter translates native display events into profile updates. Renderers use
`pxsys_display_contains_point` for clipping and hit testing and
`pxsys_display_safe_rect` as the conservative content boundary.

## Reference UI layering

The standard UI is intentionally split:

- `ui/reference` computes responsive regions and launcher grid classes in C99.
- `ui/reference/lvgl` renders those regions with LVGL.
- Future Qt, Skia, WebCanvas, or vendor renderers reuse the first layer and the
  same theme and display services.

Home and Settings register as ordinary native applications under canonical
publisher-root plus app-id identities. They claim replaceable system roles and
use the task manager's normal Intent path. Launcher entries enumerate the same
application registry, so native and PXA applications do not receive separate
namespaces or navigation APIs.

## Replacement and trimming

The reference feature mask independently includes Home, Settings, status bar,
navigation bar, and notification shade. The shade is attached to the standard
status role but can be omitted without replacing the status bar. ESP-IDF exposes the matching
`CONFIG_PXSYS_REFERENCE_UI*` options in `pxa_system_reference_ui`.

Wi-Fi and cellular presentation are independently removable with
`PXSYS_REFERENCE_UI_WIFI` and `PXSYS_REFERENCE_UI_CELLULAR` (or their ESP-IDF
`CONFIG_` equivalents). A trimmed radio has no status icon or Settings row.
Numeric battery text is independently removable with
`PXSYS_REFERENCE_UI_BATTERY_PERCENT` or
`CONFIG_PXSYS_REFERENCE_UI_BATTERY_PERCENT`; the themed battery icon and
battery service remain available.

Gesture navigation keeps a transparent bottom-edge input target even when no
indicator is drawn. The indicator is disabled by default so the application
can use the navigation reservation; enable `PXSYS_REFERENCE_UI_GESTURE_HANDLE`
or `CONFIG_PXSYS_REFERENCE_UI_GESTURE_HANDLE` to draw the handle and reserve a
small exclusive strip for it.

A product can disable the component, omit individual chrome regions, or
register a higher-priority provider for a standard role. The replacement may
be native or PXA. Standard services and protocols remain available in every
case; replacing the presentation does not fork the application model.

## Platform responsibilities

A hardware port initializes the physical profile before creating the standard
system. Board-specific geometry belongs in the product port, not in the
reference UI. For a display mode change it updates the display service from the
UI-owner thread. Custom panels may describe vendor cutouts without adding
shape-specific branches to applications.

Safe insets describe pixels that hardware geometry makes unsafe; system-bar
height and content padding are separate layout values. The compact reference
layout uses a 20-pixel status row and half-height vertical content padding so
small displays do not compound all three reservations unnecessarily.

In gesture navigation mode the window snapshot's `system_bar_insets` also cover
the bottom Home strip and the left-edge Back strip, so guest applications can
keep interactive controls outside the system gesture zones.
`pxsys_reference_layout_gesture_strip_height()` and
`pxsys_reference_layout_back_gesture_width()` are the single source of truth
for those strips.

The current ESP touch product supplies a rounded `296x240` profile. The SDL
simulator exposes equivalent CLI fields and updates them on window resize.

## System chrome and navigation

The reference status bar opens a theme-aware control center when tapped or
dragged down from the physical top edge. Its lower edge follows the pointer and
settles from the release position. Time and an ISO-style date stay in the
header; a separately scrollable body presents network tiles, level controls and
round quick actions without compressing them on small displays. Unsupported
capabilities are omitted.

Radio capability, enabled, connected, and signal-level fields live in
`pxsys_system_status_snapshot_t`, not in the LVGL adapter. Settings calls
`pxsys_system_status_service_set_network_enabled`; a product supplies the
control callback and publishes the resulting state. Alternative UI backends use
the same service. The legacy single-network fields remain a compatibility
fallback for ports that have not populated the independent radio fields yet.
Optional calendar, volume, brightness, Bluetooth, Do Not Disturb, flashlight
and airplane-mode state uses the same snapshot. Level and toggle actions call
`pxsys_system_status_service_set_level` and
`pxsys_system_status_service_set_toggle`, keeping the standard UI independent
from product-private page and driver APIs. The standard control center is a
scrollable, full-screen sheet. Opening and closing translate the complete sheet
instead of resizing it behind a fixed clip. Once its content is already at the
bottom, a new upward drag transfers from content scrolling to sheet dismissal;
the bottom Home gesture also dismisses the sheet. In button mode the existing
themed three-button navigation remains above the sheet; the control center does
not create another navigation bar. Back dismisses it without
leaving the foreground app, while Home and Recents dismiss it before performing
their normal system actions.

Navigation has two presentation modes:

- `PXSYS_NAVIGATION_BUTTONS` renders themed Back, Home, and Recents controls.
  In immersive mode a bottom swipe only reveals the transient controls; it
  never invokes Home, which still requires pressing the Home button.
- `PXSYS_NAVIGATION_GESTURES` renders a themed bottom indicator, reserves an
  edge Back gesture, and maps a bottom-edge upward swipe to the canonical Home
  role. A continuous committed swipe goes Home; pausing after the upward
  motion opens the recent-task switcher. The pause is measured from the last
  meaningful pointer movement, not from the initial press.

Both paths call the same task manager and runtime Back contract. A PXA app first
receives `PXA_WINDOW_BACK_REQUESTED`; if it does not consume the request, the
system closes its top task. Native and PXA apps therefore do not have separate
navigation protocols. In immersive mode Home remains a one-swipe action even
while transient bars are hidden. An app may consume Back for internal
navigation, but the same foreground task can consume at most two requests in a
1.8-second window; the next request closes it so an app cannot trap the user.
Recent tasks use either bounded preview cards or a
low-memory list. Tapping background outside the actual preview closes the
switcher, and upward card swipes or list close controls stop a task through the
same task manager.

On PXA foreground/background transitions the Host posts service 17 event
`PXA_SYSTEM_LIFECYCLE_EVENT` (`0x8005`) with a one-byte payload: `0` for
background and `1` for foreground. Games should stop simulation and sound on
background, clear held input, and reset their tick baseline before resuming;
the Guest may still receive completion and system events while backgrounded.
The Surface presenter stops direct scanout as soon as the task loses visibility
and only resumes on a fresh complete frame after it returns to the foreground.

Trusted chrome (visible status/navigation bars, gesture feedback, control
center and toasts) and permission/power dialogs temporarily use LVGL
composition over direct Surfaces. Direct scanout resumes after the final
overlay closes and a new frame is submitted. Recent-task previews overlay the
last presented Surface on the application snapshot; GuestMapped frames that
cannot be borrowed use an optional board capture of the displayed frame.
Previews are bounded and evicted when free memory is insufficient for another
capture. On products with tighter memory budgets, choose the list switcher.
