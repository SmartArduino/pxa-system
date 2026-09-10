# PXA Private Files Draft 0.1

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

An open result is a Component-owned `file` Handle. It is not transferable,
including to another Component of the same App. The Component uses `pxa_io`
operation `read` or `write` for sequential bytes and uses `seek` to move the
shared file position. Files do not block: a normal regular-file read returns
zero only at end of file. Closing the Core Handle closes the file exactly once.
Component stop, fault and authority revocation close all of its files through
the existing Core Handle rules.

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
