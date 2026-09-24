# PXA Window Service Draft 0.2

Window service ID is `2`; service version is `0.2.0`. Numeric assignments in this
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

Unknown required records reject the transaction; unknown optional records are
ignored. Invalid, unsupported or duplicate records reject the entire command.
The backend applies either the full candidate configuration or none of it.
`transient` means hidden by default while a system gesture may reveal the bar
temporarily; it is the platform-neutral equivalent of Android immersive system
bar behavior.

`get-snapshot` (`opcode=2`) is asynchronous and requires a nonzero request ID.
Its success data is the snapshot record list described below.

`show-toast` (`opcode=3`, since 0.2) is fire-and-forget (`request_id=0`). Its
payload is `duration-ms:u16-le | text:utf8[1..240]`. Duration must be between
500 and 5000 ms. The Host validates UTF-8, copies the message synchronously,
and shows a non-modal short notification above system navigation. This is a
best-effort hint, not a durable error report; Guests should retain actionable
errors in their own UI. An unsupported backend rejects the command.

The bottom system-bar inset reserves the visible button bar or gesture handle,
not a hidden gesture hit target. Without a gesture handle, the bottom bar inset
is zero unless a physical display safe inset independently requires space.

## Snapshot and events

Snapshot records are required singletons unless marked optional:

```text
revision=1          u64
logical-size=2      width:u32 | height:u32
pixel-size=3        width:u32 | height:u32
density=4           numerator:u32 | denominator:u32
safe-insets=5       left:u32 | top:u32 | right:u32 | bottom:u32
system-bar-insets=6 left:u32 | top:u32 | right:u32 | bottom:u32
orientation=7       unspecified | portrait | landscape
focused=8           bool:u8
```

`metrics-changed` (`opcode=0x8001`) carries a complete snapshot and is
coalescible per Component. Revision increases whenever any snapshot field
changes. A complete snapshot avoids delta loss during coalescing.

`back-requested` (`opcode=0x8002`) has an empty payload and is reliable. A
Guest event result of `handled` suppresses the Host default action; `unhandled`
requests closing the current Window/Component. This event is for system
navigation only. UI toolbar back buttons remain ordinary UI actions.
