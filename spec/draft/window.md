# PXA Window Service Draft 0.5

Window service ID is `2`; service version is `0.5.0`. Numeric assignments in this
document are explanatory. `pxa-window.json` is authoritative.

## Ownership boundary

Window controls the Component surface's relationship with the Host display:
logical metrics, density, safe insets, orientation, edge-to-edge layout,
system-bar behavior and system navigation.

A visible App header, title text, toolbar actions and page navigation are UI
nodes. They are not Window chrome. A Host may use App metadata as an invisible
task/accessibility label, but Window 0.1 does not create a title bar.

Window 0.1 is available only to the foreground UI Component. Hosts negotiate the
highest minor version in the manifest's requested range with an equal major
version, then report the selected version in startup configuration.

## Commands

`configure` (`opcode=1`, `request_id=0`) applies one record-list transaction.
Known singleton records are:

- `edge-to-edge=1`: `bool:u8`. Content uses the full surface; safe and system
  bar insets remain reported so the App can place interactive content safely.
- `status-bar-mode=2`: `u8`, one of `visible`, `hidden`, `transient`.
- `navigation-bar-mode=3`: the same modes, when a Host has such a bar.
- `status-bar-icons=4` and `navigation-bar-icons=5`: `auto`, `light`, `dark`.
- `status-bar-color=6` and `navigation-bar-color=7`: canonical RGBA8888 `u32`.
- `keep-screen-on=8` (since 0.4): `bool:u8`, only while foreground.
- `orientation-preference=9` (since 0.5): `u8`, as defined below.

Unknown required records reject the transaction; unknown optional records are
ignored. Invalid, unsupported or duplicate records reject the entire command.
The backend applies either the full candidate configuration or none of it.
`transient` means hidden by default while a system gesture may reveal the bar
temporarily; it is the platform-neutral equivalent of Android immersive system
bar behavior.

Bar colors use `0xRRGGBBAA`; alpha zero is transparent, including the default
zero value. Color and icon policy affect foreground App chrome and also apply
when transient bars are revealed. They do not paint the App's own background.
The reference Host restores its theme when native Home/Settings is foreground.
`light` and `dark` choose light and dark primary glyphs, with dimmer inactive
indicators; charging and low-battery accents retain their semantic theme colors.
`auto` is Host policy. The reference Host uses theme icons for a transparent bar,
otherwise estimates brightness from the declared color over the theme background.
It does not sample Guest frames. Apps drawing a photograph, scene or different
background underneath a transparent bar should choose the suitable icon style
explicitly and verify the rendered contrast.

The Core v1 preview accepts `configure` with a zero 64-bit request token and
a record-list payload. Its legacy Guest SDK builder emits seven records in
ascending order using exactly 41 bytes. The optional eighth record adds five
bytes; `WindowService::keep_screen_on(bool)` and `pxa_window_keep_screen_on`
can update it separately without changing system-bar settings.

`get-snapshot` (`opcode=2`) is asynchronous and requires a nonzero request ID.
Its success data is the snapshot record list described below.

`get-display-snapshot` (`opcode=5`, since 0.5) is asynchronous and requires a
nonzero request ID. It opts the Component into the extended display snapshot
for this reply, later snapshots and `metrics-changed` events. An accepted
orientation-preference configuration also opts in. Until then the 98-byte
legacy snapshot remains unchanged; the extended snapshot is 120 bytes.

`show-toast` (`opcode=3`, since 0.2) is fire-and-forget (`request_id=0`). Its
payload is `duration-ms:u16-le | text:utf8[1..240]`. Duration must be between
500 and 5000 ms. The Host validates UTF-8, copies the message synchronously,
and shows a non-modal short notification above system navigation. This is a
best-effort hint, not a durable error report; Guests should retain actionable
errors in their own UI. An unsupported backend rejects the command.
The Core v1 preview also accepts `show-toast` with a zero request token and
the same bounded payload.

`request-back` (`opcode=4`, since 0.3) has a zero request token and empty
payload. It queues the same reliable `back-requested` event as system Back,
which is delivered after the current callback returns. The App's `on_back()`
may navigate locally or return close. Hosts apply that result even when the
event is delivered by their ordinary event pump; no reentrant stop is allowed.
Acceptance does not mean the app has stopped. A full mailbox rejects the request.

System-bar insets include persistent Home/Back gesture input strips even when
their visual handles are hidden. A hidden status bar reserves no top space;
physical safe insets continue to apply independently.

## Snapshot and events

Snapshot records are required singletons unless marked optional:

```text
revision=1          u64
logical-size=2      width:u32 | height:u32
pixel-size=3        width:u32 | height:u32
density=4           numerator:u32 | denominator:u32
safe-insets=5       left:u32 | top:u32 | right:u32 | bottom:u32
system-bar-insets=6 left:u32 | top:u32 | right:u32 | bottom:u32
orientation=7       unspecified | portrait | landscape | square
focused=8           bool:u8
# The following records require the 0.5 opt-in:
rotation=9          clockwise quarter turns:u8 (0..3)
native-size=10      width:u32 | height:u32
supported-rotations=11 bit mask:u8 (bit N allows quarter turn N)
```

`metrics-changed` (`opcode=0x8001`) carries a complete snapshot and is
coalescible per Component. Revision increases whenever any snapshot field
changes. A complete snapshot avoids delta loss during coalescing.

`back-requested` (`opcode=0x8002`) has an empty payload and is reliable. A
Guest event result of `handled` suppresses the Host default action; `unhandled`
requests closing the current Window/Component. This event is for system
navigation only. UI toolbar back buttons remain ordinary UI actions.

## Direction policy (since 0.5)

Native orientation is the board's normal reading orientation after its fixed
panel-mounting correction. It need not be portrait. Rotation 0 is native;
1, 2 and 3 rotate graphics clockwise into the native panel by 90, 180 and
270 degrees. Physical device rotation is the opposite direction, as with
Android `Display.getRotation()`. A Host may present the logical viewport in a
resized emulator window instead of physically rotating the window's pixels.

Portrait means current logical height > width; landscape means width > height.
Equal dimensions mean square. Never infer shape from rotation alone. A native
854×480 panel starts landscape, a 480×854 panel starts portrait, and a square
panel remains square at every rotation. Legacy snapshots encode square as
unspecified; extended snapshots report square explicitly.

| Preference | Value | Result |
| --- | --- | --- |
| system | 0 | Follow the Host system direction (currently native on device Hosts) |
| portrait / landscape | 1 / 2 | Choose the matching shape's canonical angle |
| reverse-portrait / reverse-landscape | 3 / 4 | Choose that shape with another 180 degrees |
| native | 5 | Rotation 0 |
| rotation-0 / 90 / 180 / 270 | 6 / 7 / 8 / 9 | Exact angle relative to native |
| locked | 10 | Capture the current angle at request time |

For a non-square panel, canonical means 0 if the native shape already matches,
otherwise 1. If that angle is unavailable but its opposite is supported, a
shape preference may use the opposite. An exact angle never silently falls
back. Unsupported preferences reject the entire configuration transaction.
For a square panel all four shape preferences retain its current angle;
explicit angles are available when the App's reading direction matters.
Locked is stored as an exact angle, so leaving and returning to the App does
not recapture the system angle.

The App can change its request during execution, including from an in-game
settings page. The request belongs to the foreground UI task; Home restores
the system direction, task resume reapplies the retained request, and releasing
the Window restores system direction. No accelerometer-driven automatic
rotation is implied by these preferences. Hosts without a rotation port expose
only bit 0; Apps must query capabilities and handle rejection.

Hosts rotate rendering, physical pointer coordinates, safe insets, corner
radii and cutout bounds together, and cancel old pointers at a coordinate
change. Injected PXADB coordinates and screenshots use the current logical
viewport. Odd quarter turns swap width and height. Every accepted angle change
updates the snapshot revision, including 180 degrees and square panels, and
updates the UI display environment so reactive layouts can reflow. Apps should
consume actual metrics rather than assume their request was accepted.

Motion sensors remain in the native, right-handed device frame: +X right,
+Y top, +Z out. For tilt control in the current reading view use
`pxa_display_motion_to_screen(device_xyz, snapshot.rotation, view_xyz)` on
acceleration, angular velocity or magnetic vectors. At rotation 1 it returns
(-Y, X, Z). Do not apply raster Y-down transforms to sensor samples, or rotate
a vector twice. Absolute orientation/Euler angles require their own rotation
composition; the vector helper does not convert Euler angles.
