# PXA Installer Transactions Draft 0.1

The installer is a Host security boundary. Package input is untrusted even
when it is a local directory. Parsing a manifest does not authorize a file to
be opened, and a valid publisher signature does not make undeclared files safe.

## Storage domains

Installed code and private data use distinct roots:

```text
packages/<app-id>/
packages/.session-<app-id>/
owners/<app-id>
data/<app-id>/
```

The App directory is the only active, verified Package directory. During an
installation or update, the installer creates a `.session-<app-id>` directory
containing its transient `incoming` and `old` directories, then removes the
whole session after commit or recovery. It never reads, renames or removes the
`data` tree. Uninstall and private-data retention are separate policy operations.

`owners/<app-id>` is Host-managed identity metadata. Its canonical 40 bytes
are `"PXAO" | major:u16=1 | minor:u16=0 | lineage_root_key_id:32`. The
installer creates it when an App ID is first claimed and retains it across
uninstall and data clearing. A later Package using the same App ID must resolve
to that exact lineage root. This prevents an unrelated trusted publisher from
claiming retained private data, permission decisions or enable policy. A valid
PXKL rotation keeps the same root and therefore keeps ownership. Corrupt or
unowned legacy data fails closed and needs explicit product recovery.

All names below the storage root are derived from canonical manifest fields.
`app-id` is globally unique, like an Android application ID; Package path
segments retain the restrictions in `package.md`.

## Secure source traversal

For directory input, the Host opens the source root once and resolves every
entry relative to that root without following links. Every path component must
be a real directory and every payload must be a regular file. Symlinks,
hard-linked files, devices, sockets, FIFOs, sparse size mismatches and entries
not declared by the manifest are rejected.

The Host reads `manifest.pxm` and `signature.pxs` with explicit size limits,
parses the exact bytes, verifies the publisher signature, then enumerates the
entire source tree. It copies each declared file into `incoming` while
streaming SHA-256 and enforcing the declared byte count. A metadata check after
the final read must still describe the same regular file. Mutable source media
may therefore cause installation to fail, but cannot substitute unverified
bytes.

Reserved metadata files are not payload entries. The source tree contains
exactly `manifest.pxm`, `signature.pxs`, declared regular files and their
implicit directories. Empty undeclared directories are rejected as
non-canonical input.

## Transaction protocol

Only one transaction may operate on an App identity at a time. The Host must
hold an identity-scoped exclusive lock for recovery, installation and
activation. The active App directory and its `.session-<app-id>` directory must
reside on one filesystem whose directory rename is atomic. The transaction
algorithm calls the active directory, session `incoming` and session `old`
its logical slots `current`, `incoming` and `previous` respectively.

The installer core is expressed in terms of three verified directory slots and
five storage operations: inspect, remove, rename, durability barrier and
identity lock. A platform adapter must advertise and satisfy these properties:

- a successful rename is atomic and recoverable after remount;
- files written to `incoming` have a durability barrier before activation;
- operations are serialized for one App identity;
- slot inspection distinguishes absent, verified and corrupt directories;
- all three slots reside in one filesystem and trust domain.

The Core does not emulate a missing property with optimistic behavior. A Host
whose filesystem cannot provide atomic rename and durable file flush cannot be
used as writable Package storage.

On a POSIX Host, the adapter uses descriptor-relative no-follow traversal, an
advisory identity lock, file `fsync`, directory `fsync` and atomic rename. On
ESP32 LittleFS, links do not exist in the filesystem model, VFS file `fsync`
maps to `lfs_file_sync`, and LittleFS rename commits metadata atomically. The
ESP adapter uses an in-process identity mutex because the PXA Host is the only
Package writer. It must not claim the same guarantees for SPIFFS, FAT or an
arbitrary VFS mount merely because they expose similarly named C functions.

An ESP directory source must be a canonical absolute path below an explicitly
configured source root and outside the managed `packages` subtree. Source and
destination may use separate ownership roots on one LittleFS mount. This
prevents a transaction from copying one of its own slots or deleting its source
during recovery.

The ESP integration configures one canonical mutable state root outside the
system-assets transaction roots and gives it fixed `inbox`, `packages` and
`data` children. It bounds one scan to 32 inbox entries and 64 installed App
identities. Exceeding a bound is reported rather than silently truncating an
otherwise successful scan.

Installation performs these durable phases:

1. recover any interrupted transaction;
2. create a new empty `incoming` directory;
3. securely copy and verify the complete Package into `incoming`;
4. flush payload files, metadata files, directories and their parent;
5. remove an older session `old`, if present;
6. rename the active App directory to session `old`, when it exists;
7. rename verified session `incoming` to the active App directory and flush the parent directory;
8. remove `.session-<app-id>` and flush the parent directory.

After source verification and transaction recovery, an installer may return
the existing verified active App directory without starting a new transaction only when
the exact canonical `manifest.pxm` bytes are identical. This makes persistent
inbox scanning idempotent without treating display-version text as a content
identity. Payload equality follows from the signed manifest inventory.

The activation commit point is step 7. Failure before it leaves the prior
active App directory authoritative. If step 7 fails after step 6, the Host restores
session `old` to the active App directory before returning when possible; otherwise recovery does
so at the next operation. Updating an existing identity requires the incoming
manifest to have the exact same `(lineage_root_key_id, app_id)` management
tuple and a permitted lineage transition. Version ordering is policy and is
not inferred from display-version text.

## Crash recovery

Recovery inspects only complete directory names. It fully verifies a staged
session `incoming` directory before it can be activated. For the active directory and
session `old`, a Host may instead rely on its installed-storage trust boundary and
parse the manifest only; a Host without that boundary must re-verify them.
Invalid directories are quarantined or removed; they are never activated. With
`C`, `P` and `I` denoting the verified active App
directory, session `old` and session `incoming`, recovery applies these rules:

| Present state | Recovery result |
| --- | --- |
| `C` only | keep `C` |
| `C`, `P` | keep `C`; discard `P` |
| `C`, `P`, `I` | keep `C`; discard `P` and `I` |
| `P`, `I` | restore `P` to `C`; discard `I` |
| `C`, `I` | keep `C`; discard uncommitted `I` |
| `I` only | no active Package; discard `I` |

An invalid `C` is never hidden by an automatic upgrade to `I`. The Host reports
storage corruption and requires explicit repair policy. This keeps crash
recovery deterministic and prevents a partially written directory from gaining
authority.

Uninstall holds the same identity lock and removes the active directory and
any transaction siblings.
It does not remove the ownership record, identity-scoped private data, cache,
secrets, enable policy, or a system-owned Package source. Clearing private data
is a separate product management operation and also retains the ownership
record, so uninstall or clearing data cannot transfer an App ID to a different
publisher implicitly.

Recovery is implemented once by the platform-independent slot transaction
state machine. Platform adapters must not add alternate recovery precedence.
Every state transition is followed by the backend durability barrier before a
subsequent transition begins.

## Package visibility

Runtime activation receives a read-only Package root corresponding to the
verified active App directory. An update does not mutate a running Component's
files in place. The Host stops old instances and opens the new active directory before
creating replacements. Existing open files may remain valid internally until
old instances are destroyed, but are never exposed as transferable App
handles.
