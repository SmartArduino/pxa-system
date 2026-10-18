# PXA Files Draft 0.3

FS 0.3 preserves private-file operations 1–8 and adds optional scoped directory
roots. The existing private path and quota rules below remain unchanged.
`pxa-fs.json` is the version/opcode authority. Implemented extensions are
OPEN_ROOT (9), REQUEST_ACCESS (10), OPEN_AT (11), STAT_AT (16) and QUERY_ROOT (17),
gated by `scoped-roots`, `user-directory-grants` and `app-media-root` feature bits. Optional
`read-at` (service bit 1, regular-file bit 0) adds bounded positional reads. Other proposed
multi-volume operations are not implemented or advertised yet.

User roots require a signed `fs.user-directory` declaration, exact scope `read`
or `read-write`, and a live Permission handle. Its Core authority binds every
root, descendant and pending picker. The Host intersects this with the specific
directory grant; a permission declaration alone does not authorize a path.
Grant IDs are opaque 16-byte per-identity records; runtime handles are DIRECTORY
type 4 or FILE type 3 and must never be persisted. Closing a parent directory
does not close its independently owned children. Permission revocation does.

OPEN_ROOT uses `kind:u8|reserved:3=0|permission:u64|grant-id:16` (28 B).
Private/cache/temp kinds reserve IDs 1/2/3; the initial scoped provider supports
grant kind 4. With feature bit 6, kind 5 opens the verified app's fixed media
root. Permission and GrantId must both be zero for kind 5: the Host resolves
the app identity and configured storage policy, without a picker or a broad
storage permission. It cannot accept a Guest-selected volume or native path.
An unavailable configured volume returns unavailable, never a fallback root.
The initial ESP mapping is `/PXA/media/<app-key>` on the board-selected SD
volume; future installation placement supplies that selection. This is shared
media storage, not confidential private saves. Its initial rights are read and
enumerate. Its success data is `directory:u64|rights:u32`. REQUEST_ACCESS uses
`permission:u64|rights:u32|flags:u32|purpose-length:u16|purpose:utf8` and returns
`directory:u64|rights:u32|grant-id:16`. Flag 1 requests persistence. Session-only
access returns a zero GrantId. The picker is trusted Host UI; Core cancellation,
revocation or stop prevents a late UI decision from publishing a handle.

OPEN_AT uses `directory:u64|flags:u32|path-length:u16|path:utf8`; success is
`handle:u64|kind:u8|reserved:3=0|file-features:u64|size:u64` (28 B). STAT_AT uses
`directory:u64|path-length:u16|path:utf8`; success is `kind:u8|reserved:7=0|size:u64`.
QUERY_ROOT uses one DIRECTORY u64; its 72-byte success layout is in the JSON.
Success lengths exclude the common status. Errors have only status:i32.

Scoped paths are bounded UTF-8 relative paths. The protocol ceilings are
1024 B total and 255 B per segment, with smaller backend limits reported by
QUERY_ROOT. Absolute paths, NUL, dot/parent segments, backslash, colon, trailing
dot/space and case variants of `.pxa-*` segments are rejected. Native FAT also
rejects short-alias `~` spellings. A Host may use that backend only on a link-free
mount without nested mounts; POSIX instead opens directory descriptors with
no-follow traversal and refuses non-regular or hard-linked files.

Every read/open/seek checks the original mount epoch, grant revision and rights.
Duplicate volume identities are ambiguous and inaccessible. Remounting cannot
revive an old handle. The ESP adapter reports read-only roots and supports
FILE READ, FS SEEK and optional READ_AT. It does not promise hotplug. No SD
installation guarantee follows from data-read support.

READ_AT uses FILE I/O operation 16 with an exact
`offset:u64|cookie:u64|requested:u32|reserved:u32=0|data[requested]` buffer.
The return value is the number of data bytes (zero at EOF); data starts at
byte 24. Requests contain 1–4096 data bytes. The sequential cursor is unchanged.
Nonzero cookies must increase for each FILE; completed calls retire their
cookie, including backend errors. Invalid headers and overflowing ranges are
rejected before retirement. Reusing a retired cookie returns cancelled.
Operation 17 accepts exactly one nonzero cookie:u64 and idempotently retires
cookies up to that value. Closing the FILE invalidates its entire generation.

This provider profile is bounded and synchronous: no retained Guest pointer,
worker, pending lane, WOULD_BLOCK or async readiness is advertised. The future
asynchronous lane proposal requires separate completion/readiness integration;
it is not implemented by pretending a synchronous callback is asynchronous.
The Host callback is an optional roots-backend tail, checked by struct_size;
old providers retain their existing layout and feature behavior. READ_AT is
advertised per regular file only when the provider implements it. RootInfo
capability bit 0 and max_read_at_bytes=4096 describe this support.

C and C++ SDK calls borrow a caller-owned header/data packet and perform no
allocation or result copy. C also exposes the existing opcode 8 through
build_replace, sharing the canonical path builder with rename.

FS v1 exposes only the private, persistent App data domain. A Host maps one
FS service instance to one verified `(publisher-key-id, app-id)` identity;
every Component in that App sees the same tree. Package assets, system assets,
cache, temporary files, user-granted documents and Secrets are separate
services or domains and are not addressable through FS v1.

The Guest never sees a Host path, descriptor or filesystem implementation.
All names are relative to the App private root and identify bytes, not a
platform-normalized display name. The Host must not apply Unicode case folding
or normalization.

## Paths

A path is valid UTF-8, between one and 255 bytes, and consists of `/`-joined
segments. A segment is between one and 64 bytes, cannot be `.` or `..`, and
cannot contain NUL, ASCII control characters, `\\` or `/`. Paths have no
leading or trailing `/` and no empty segment. The byte form is canonical: two
different UTF-8 byte sequences are two different names even if a UI happens to
render them alike.

Names whose segment begins `.pxa-` are Host-reserved and are invalid in FS v1.
They allow independently versioned private-data services to keep recovery state
outside the Guest namespace and FS logical-byte accounting.

Every traversal stays within a Host-owned private-root boundary. A POSIX Host
uses descriptor-relative traversal; a linkless filesystem Host validates the
canonical relative path before combining it with the Host-owned root. A Host
rejects a symbolic link, device, socket, FIFO, unsupported file kind or a
regular file with more than one link when those kinds exist. It never resolves
a guest path through a link. These rules apply to every existing parent and
final entry.

## Requests and Handles

`pxa-fs.json` assigns service 5 version 0.1.0. `open`, directory mutation,
`stat` and `seek` are ordinary asynchronous Core requests. Their result begins
with the common `status:i32`; a successful `open` adds `handle:u32`, and a
successful `stat` adds `kind:u8|size:u64`.
The Core v1 preview returns a native `handle:u64` instead. `seek` and
`read-directory` accept that same 64-bit Handle in their request payloads;
typed `pxa_io` and Core close validate its full generation. The v0 layouts
remain available only to Core v0 Components.

An open result is a Component-owned `file` Handle. It is not transferable,
including to another Component of the same App. The Component uses `pxa_io`
operation `read` or `write` for sequential bytes and uses `seek` to move the
shared file position. Files do not block: a normal regular-file read returns
zero only at end of file. Closing the Core Handle closes the file exactly once.
Component stop, fault and authority revocation close all of its files through
the existing Core Handle rules.
The desktop product simulator binds its POSIX private-file root to the signed
publisher key ID and App ID, separate from Storage recovery data.

`open-flags` has read, write, create, exclusive, truncate, append and
directory bits. The directory bit opens an existing directory Handle and may
only be combined with read. `read-directory` returns at most one entry from
that Handle per request: `entry-name`, `entry-kind` and `entry-size`, or a
zero-length `end-of-directory` record. Entry order is unspecified. This small
page size keeps results bounded and does not expose a platform directory cursor.

For regular files, at least read or write is required; exclusive requires
create; truncate and append require write.
A Host creates new files with private permissions only.
`remove` removes a regular file or an empty directory; it returns `busy` when
the target regular file is still open in the App. `rename` does not replace an
existing destination in v1. `make-directory` creates exactly one directory;
parents must already exist.

## Quota and Failure Semantics

Each App has a Host-assigned byte quota, reported as a startup resource limit.
Usage is the sum of logical sizes of regular files in the App private tree;
directory metadata, LittleFS encoding overhead and free-space estimates are
not Guest-visible quota units. A write that would exceed the quota fails with
`quota-exceeded` and writes no bytes. Truncation releases logical usage before
the successful open result. Removing a closed regular file releases its size.

No FS v1 operation recursively removes a tree. A failed request makes no
partial protocol-visible result. A Host may report `resource-limit` before
opening a file when its component or App file-handle limit is exhausted.

FS 0.2 adds optional `replace` (opcode 8), with the same path records as `rename`.
It atomically publishes a completed regular file over a closed regular file in
its private filesystem, or into an absent destination. Directories, links and
open source/destination files are rejected. Failure leaves the old destination
intact. A backend without replacement support returns `unsupported`. The POSIX
and ESP LittleFS adapters use the filesystem rename operation and subtract the
replaced file from disk quota usage after success. No body buffer is allocated.
