# PXA Package Format Draft 0.1

Package Format is versioned independently from the PXA Core and service ABIs.
The authoritative numeric schema is `pxa-package.yaml`.

## Logical Package and trust boundary

A directory Package has two reserved root entries:

```text
manifest.pxm   canonical binary manifest
signature.pxs  detached signature envelope
```

Every other regular file must appear exactly once in the manifest file table.
Directories are implicit. Paths use ASCII letters, digits, `/._-` and are
case-sensitive. Symlinks, devices, hard-link aliases, absolute paths,
backslashes, empty path segments, `.` and `..` segments are forbidden. Hosts
must preserve and compare the exact signed path bytes.

Installation proceeds in this order:

1. bound and parse `manifest.pxm` without following package paths;
2. resolve the publisher key from the device trust store;
3. verify `signature.pxs` over the exact manifest bytes;
4. enumerate payload entries and reject missing or unlisted files;
5. stream each file through SHA-256 while enforcing its declared size;
6. stage to a private directory, re-verify, then atomically activate.

The canonical single-file archive encoding uses the `.pxa` extension and is
defined by `container.md`. It embeds these same exact logical metadata files,
adds a signature over the complete delivery representation and decodes to the
same installed directory. A `.pxa` is never a mounted runtime filesystem.

No Artifact is loaded before all these checks succeed.

## Canonical manifest encoding

The manifest header is:

```text
magic:"PXAM" | major:u16=0 | minor:u16=1 | body_len:u32
```

The body is a Core-style record list (`tag:u16 | length:u16 | payload`). Records
must be in ascending encoded-tag order. Repeated records are contiguous and
sorted by their identity key. Known fields must use their required tag encoding;
setting the optional bit on a known field is non-canonical. Unknown required
records reject the manifest; unknown optional records may be skipped but still
participate in the signature.

Required top-level records identify the publisher key, App ID, version and Core
version range. Repeated Component, file, permission and IPC endpoint records describe the
complete package. Integers are little-endian. IDs, paths and compatibility
identifiers use restricted ASCII; display metadata is strict UTF-8 without NUL
or control characters.

A Component service requirement contains a service ID, inclusive version range
and required feature bitset. A permission contains a globally registered dotted
name, a required/preferred flag and optional service-defined canonical scope
bytes. Unknown fields inside nested records follow the same required/optional
rule as top-level fields.

An IPC endpoint contains a canonical endpoint name and the Component that
provides it. Both are signed metadata. Endpoint names are unique in one
Package and their targets must be declared Components. Draft IPC v1 routes only
inside one activated signed App. At App launch, a Host activates the foreground
`main` UI Component, then activates every Component named by an IPC endpoint
before registering that endpoint. Components without an endpoint remain
inactive until a future lifecycle trigger defines otherwise. Cross-App routing,
endpoint discovery, streams and ACL policy are separate extensions.

Core and Service ranges have one fixed major version and an inclusive minor
range. A range cannot cross a major boundary. Component activation validates
Core, every required Service and its feature subset before selecting or loading
an Artifact.

The detached signature header is:

```text
magic:"PXAS" | version:u16=0x0001 (0.1.0) | algorithm:u16 |
publisher_key_id:32 bytes | signature_len:u16 | reserved:u16=0 | signature
```

Draft 0.1 algorithm 1 is ECDSA P-256 over SHA-256, encoded as fixed 64-byte
`r || s`. Both scalars must be in the valid P-256 range and `s` must use the
low-S form, so one signature has one accepted encoding. The digest input is the
ASCII domain `PXA-PACKAGE-MANIFEST`, one zero byte, then the exact
`manifest.pxm` bytes. The key ID is SHA-256 of the publisher public key's
canonical DER SubjectPublicKeyInfo and must equal the signed publisher ID
inside the manifest.

## Identity and private data

App identity is the tuple `(publisher_key_id, app_id)`. Version, display name,
Component IDs and Artifact choices do not affect identity. Private data,
permissions and IPC publisher ownership are keyed by this tuple.

An update may replace an installed App only when this identity matches. Key
rotation requires a separately specified, trusted transition statement; merely
putting a new key ID in a package creates a different App and never inherits
the old App's data.

The Manifest 0.2 publisher-lineage profile in `container.md` defines
that transition statement. After a verified rotation, management identity is
the tuple `(lineage_root_key_id, app_id)` while `publisher_key_id` identifies
the current signer. Manifest 0.1 retains the identity rule above.

Uninstall policy is separate from package removal: retained data remains bound
to the same identity and cannot be claimed by another publisher using the same
App ID.

## Components and Artifacts

A Package contains one or more Components. Component kinds are `ui`, `service`
and `job`. A Package has at most one UI Component. Each Component has one or
more alternative Artifacts:

- `wasm`: portable WebAssembly, independent of target and engine ABI;
- `aot`: target- and engine-specific native code produced from the same
  Component.

Every Artifact declares a package path, required Wasm feature bits and memory
model. AOT additionally requires exact `target`, `engine` and `engine_abi`
strings. `engine_abi` includes all code-loading compatibility inputs, including
engine version, AOT format, calling convention and relevant build options.
Draft 0.1 registers feature bits for bulk memory, reference types, SIMD,
multi-value, mutable globals, sign extension, non-trapping float conversion and
threads. Unknown required bits make an Artifact incompatible rather than making
the Package malformed.

Different Components never link through shared linear memory. Libraries are
part of an Artifact; independently instantiated `.wasm` and `.aot` Components
communicate through PXA IPC.

The capability-only WASI Service requirement declares a Preview 1 reactor
built against `wasi-libc`. Its feature bits describe ambient resources, not
ordinary C library algorithms. A Host may publish the Service with no feature
bits to support pure libc computation. It must reject a module when an actual
`wasi_snapshot_preview1` import is absent from the signed feature subset, and
must reject imports from unregistered modules such as `env`. A WASI context
starts with no preopened directories, arguments or environment. Publishing a
base WASI context may bind descriptors 0, 1 and 2 to a Host-owned null device
to satisfy retained wasi-libc formatting imports; this does not constitute the
`stdio` feature because no Host input or output is observable. Publishing a
resource feature does not replace the corresponding product user-permission
decision; both checks are required before a Host may attach that resource.
Clock reads and entropy generation are non-interactive primitives: their
signed WASI feature requirement is sufficient authorization and does not
create a user prompt. This exception does not extend to files, network,
sensors, audio or other identity- or user-bound resources.
Preview 1 carries the clock ID as a runtime argument to the shared
`clock_res_get` and `clock_time_get` imports. A Host that exposes these imports
must therefore require both `monotonic-clock` and `wall-clock`; advertising or
requesting only one of those bits does not authorize the shared imports.

## Deterministic selection

For one Component the Host:

1. filters Artifacts by memory model and required feature subset;
2. keeps AOT only on exact target, engine and engine-ABI matches;
3. chooses a compatible AOT before portable Wasm;
4. within one kind, chooses the candidate requiring the greatest number of
   supported feature bits, then the lexicographically smallest path;
5. returns `unsupported` when no Artifact is compatible.

Artifact file order in an archive and manifest record order cannot influence
selection. A selected file is still passed to the engine's own bounded module
validator before instantiation.

Draft 0.1 bounds one Package to 32 Components, 16 Artifacts and 32 Service
requirements per Component, 128 payload files and 64 permissions. A Host may
apply smaller advertised installation limits but cannot silently truncate.
