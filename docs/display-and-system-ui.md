# Display Profiles and Standard System UI

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

The current ESP touch product supplies a rounded `296x240` profile. The SDL
simulator exposes equivalent CLI fields and updates them on window resize.

## System chrome and navigation

The reference status bar opens a theme-aware notification shade when tapped or
dragged down from the physical top edge. The current shade presents system time,
network and battery state plus an empty notification state. A notification
storage/delivery service can populate it later without changing its role or
input contract.

Navigation has two presentation modes:

- `PXSYS_NAVIGATION_BUTTONS` renders a themed Settings/Back control.
- `PXSYS_NAVIGATION_GESTURES` renders a themed bottom indicator, reserves an
  edge Back gesture, and maps a bottom-edge upward swipe to the canonical Home
  role.

Both paths call the same task manager and runtime Back contract. A PXA app first
receives `PXA_WINDOW_BACK_REQUESTED`; if it does not consume the request, the
system closes its top task. Native and PXA apps therefore do not have separate
navigation protocols. A future recents/task-switcher can claim its own system
role and extend the bottom gesture state machine without changing app identity
or Intent routing; it is not implemented by the current Home gesture.
