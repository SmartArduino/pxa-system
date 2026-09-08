# PXA UI and Canvas

UI is a backend-neutral retained-tree service. Window owns display metrics,
safe-area insets and system navigation. UI owns the widget tree, layout,
theme, package resources and interaction state. Canvas is reserved for games
and custom drawing. No toolkit objects, paths outside the signed Package, or
callback pointers cross the Core ABI.

## Transactions

A Component has one active scene and at most one candidate transaction.
The begin operation carries a strictly increasing generation and selects reset
or patch. Batch carries length-prefixed create, set-property, clear-property,
move and remove commands. An unknown required command rejects the transaction;
an unknown command with the optional flag is skipped. A new begin, cancel,
Component stop or unbind discards the existing candidate.

Properties may be appended over multiple batch messages. The Host validates
the complete candidate, including one Screen root, unique node IDs, parent
types, cycles, depth, UTF-8, package paths and all quotas. Commit changes the
active generation only after both validation and backend acceptance. Any
failure leaves the active scene untouched.

The fixed node set is Screen, Container, Scroll, Text, Icon, Button, Image,
Progress, Switch, Slider and Canvas. Containers support row, column and stack
layout, wrapping, alignment, gap, padding, grow/shrink, pixel/percent/content/
fill sizes, constraints and absolute positioning.

## Theme and interaction

Colors may refer to the background, surface, primary, on-primary, text, muted,
border, success, warning and danger tokens, or provide an RGBA override. The
Host resolves tokens against the current light/dark theme and reapplies them
when the theme changes. Retained text roles are caption, body and title;
Canvas also supports an icon role that uses the Host icon font. Built-in Icon
IDs use the same icon font.

Buttons, switches, sliders and scrolling update immediately on the Host.
Guest patches may later override their values. Event 0x8001 reports node,
click/value-change/long-press/scroll kind and value. Click and long-press are
reliable; value-change and scroll may be coalesced by Component, node and kind.

## Canvas frames

Canvas uses an independent, strictly increasing frame generation and two Host
buffers. Canvas-begin resets the staging buffer, one or more canvas-append
messages stream length-prefixed primitives, and canvas-present validates and
atomically swaps it. Present may carry up to four dirty rectangles.

Primitives are RGBA rectangle, ellipse, line, arc, UTF-8 text, Package PNG,
clip push and clip pop. Text and frames are limited by negotiated startup
limits rather than a Guest SDK compile-time constant. Pointer event 0x8002
carries node, pointer ID, down/move/up/cancel phase, buttons, signed 32-bit
local coordinates and a monotonic timestamp. Down/up/cancel are reliable;
move is coalescible.

## Device profile

The initial ESP32-S3 profile is 96 nodes, depth 12, 24 KiB property storage,
8 KiB aggregate text, 2 KiB per text value, two Canvas nodes, 16 KiB and 1024
primitives per frame, 128-byte resource paths, and PNG resources no larger
than 256 KiB or 512 by 512 pixels. Hosts publish these limits in startup
configuration. Simulator defaults match the device profile.
