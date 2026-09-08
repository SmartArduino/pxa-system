# PXA Container Format Draft 0.1

The PXA Container is the canonical single-file transport and installation
encoding for a signed Package. Its file extension is `.pxa`. It does not
replace the Package manifest, installed directory layout or installer slot
transaction defined by `package.md` and `installer.md`.

The design has four goals:

1. authenticate container metadata before it reaches a decompressor;
2. install with bounded memory and sequential IO on small flash devices;
3. keep installed Artifacts and assets as ordinary read-only Package files;
4. make every `.pxa` a complete, independently installable Package.

`pxa-container.yaml` is authoritative for numeric assignments and fixed
structure sizes.

## Package and delivery layers

A logical Package consists of the exact canonical `manifest.pxm`, its detached
`signature.pxs`, and every file inventoried by that manifest. This logical
signature remains valid for a built-in directory and for the installed
directory after extraction.

A `.pxa` adds a second signature over the exact delivery representation. The
two signatures have different responsibilities:

- `signature.pxs` authenticates App identity, compatibility, permissions and
  the digest of every decoded payload file;
- the `PXCS` container signature authenticates the container header, logical
  signature, compression records and exact encoded payload bytes.

Both signatures use the same publisher key. A Host rejects a container unless
the key IDs in its Manifest, Package signature and container signature are
identical.

The extra signature is intentional. A decoder must not treat compression
metadata as trusted merely because the bytes it might eventually produce have
signed digests.

## File layout

All integers are unsigned little-endian. Sections are adjacent, with no
alignment padding and no trailing bytes.

```text
offset                                      section
0                                           PXAC header (64 bytes)
64                                          manifest.pxm
64 + manifest_size                          signature.pxs
+ package_signature_size                    PXCS signature envelope
+ container_signature_size                  encoded payload
+ payload_size                              end of file
```

The actual file size must equal the signed `container_size`, where:

```text
container_size = 64 + manifest_size + package_signature_size
                   + container_signature_size + payload_size
```

Checked arithmetic must be used for every offset and size calculation.

## PXAC header

The fixed header is:

```text
offset  size  field
0       4     magic = "PXAC"
4       2     major = 0
6       2     minor = 1
8       2     header_size = 64
10      2     flags = 0
12      2     codec
14      2     chunk_size_log2 = 12
16      4     manifest_size
20      4     package_signature_size
24      4     container_signature_size
28      4     file_count
32      8     payload_size
40      8     unpacked_size
48      8     container_size
56      4     reserved = 0
60      4     header_crc32
```

`header_crc32` is the IEEE CRC-32 of bytes `[0, 60)`, with initial and final
XOR `0xffffffff`. It is an early corruption check, not an authenticity check.
The container signature covers the complete header including this CRC.

Draft 0.1 defines no flags. A nonzero flag, reserved field, incompatible
version or noncanonical section size rejects the container.

`manifest_size` is in the range accepted by the logical Package format and is
at most 16384 bytes in Draft 0.1. The two signature sizes are exactly 108 bytes
for the Draft 0.1 P-256 envelopes. `file_count` equals the signed Manifest file
count. `unpacked_size` equals the checked sum of all Manifest file sizes and
does not include the Manifest or either signature envelope.

The header is untrusted until the container signature succeeds. Before that
point it may only be used after applying Host hard limits and overflow checks.

## Codecs

Codec 0 is `store`; every chunk is encoded verbatim.

Codec 1 is independent LZ4 blocks. Each block has no dictionary dependency on
an earlier block. A packer stores a block verbatim whenever LZ4 does not make
it smaller. A decoder therefore needs one 4096-byte input buffer and one
4096-byte output buffer; the output buffer may be the installer's existing IO
buffer.

Draft 0.1 fixes `chunk_size_log2` to 12, or 4096 decoded bytes. A different
chunk size requires a later compatible container version even if an underlying
codec library can decode it.

The PXA encoding is not an LZ4 Frame. It uses raw independent LZ4 blocks inside
the bounded chunk records below. This avoids a 64 KiB history or frame buffer
and makes decoded size limits explicit before each decoder call.

## Encoded payload

The payload contains exactly one file record for each signed Manifest file, in
the same order as the canonical Manifest file table. Paths are not repeated in
the container. The destination path always comes from the signed Manifest.

Each file begins with a 16-byte record:

```text
offset  size  field
0       2     file_index
2       2     flags = 0
4       4     chunk_count
8       8     encoded_size
```

`file_index` starts at zero and increases by one. `encoded_size` counts all
chunk headers and chunk data following this record, but not the file record
itself. It lets a verifier bound the record; it does not authorize skipping
file digest validation.

Each chunk is:

```text
offset  size          field
0       2             stored_size
2       2             decoded_size
4       stored_size   data
```

Rules for every file are:

- `chunk_count` is zero exactly when the signed file size is zero; otherwise
  it is `ceil(file_size / 4096)`;
- `decoded_size` is 4096 except for the final partial chunk, and the sum of
  decoded sizes equals the signed file size;
- both sizes are nonzero for a present chunk and at most 4096;
- for codec 0, `stored_size` equals `decoded_size`;
- for codec 1, equality denotes verbatim data and a smaller `stored_size`
  denotes one raw LZ4 block;
- `stored_size > decoded_size` is noncanonical and rejected;
- LZ4 decoding must use a bounded safe decoder and return exactly
  `decoded_size` bytes;
- the sum of chunk headers and stored bytes equals the file record's
  `encoded_size`;
- the sum of file records and encoded file bodies equals `payload_size`.

An implementation rejects missing, duplicate, reordered or additional file
records. It also rejects bytes after the last declared chunk.

## Container signature

The container signature envelope is:

```text
magic:"PXCS" | version:u16=0x0001 (0.1.0) | algorithm:u16 |
publisher_key_id:32 bytes | signature_len:u16 | reserved:u16=0 | signature
```

Algorithm 1 is ECDSA P-256 over SHA-256 with the same fixed 64-byte P1363
`r || s`, scalar validation and low-S requirements as `signature.pxs`.

First compute `material_digest = SHA-256(...)` over the following exact byte
concatenation:

```text
complete 64-byte PXAC header
exact manifest.pxm bytes
exact signature.pxs bytes
exact encoded payload bytes
```

The ECDSA signature input is then:

```text
ASCII "PXA-PACKAGE-CONTAINER-DIGEST"
one zero byte
material_digest:32 bytes
```

This two-stage definition lets a bounded-memory Host use its existing
domain-separated P-256 verifier after streaming the exact container material.

The `PXCS` envelope itself is excluded. Its canonical algorithm, length,
reserved field and key ID are independently checked before signature
verification.

A standard installer performs two sequential payload reads: one to verify the
container signature before decompression and one to extract. This is preferred
over decoding attacker-controlled compressed bytes. The Package sizes targeted
by Draft 0.1 make the extra flash read cheaper than the extra security surface.

## Canonical encoding

For one logical Package and one selected delivery profile, a canonical packer:

1. orders files exactly as the canonical Manifest;
2. splits each file at 4096-byte boundaries;
3. resets LZ4 state for every chunk;
4. emits compressed data only when it is strictly smaller than the input;
5. emits no timestamps, permissions, owner IDs, path copies or padding;
6. fills every size field from the final byte representation;
7. computes the header CRC, then signs the exact container message.

Compression tool version and level are build metadata, not container fields.
Changing either may change the signed `.pxa` bytes without changing the
logical Manifest. Release tooling should pin them when byte-for-byte
reproducibility is required.

## Verification and installation

A writable Host installs a staged `.pxa` in this order:

1. open the source once and capture immutable-source metadata where available;
2. read and bound the fixed header using checked arithmetic;
3. read and parse `manifest.pxm`, validate any publisher lineage to resolve the
   current signing key, then verify `signature.pxs`;
4. parse the `PXCS` envelope and require all three publisher identities to
   match;
5. stream the exact header, Manifest, Package signature and payload into
   `material_digest` without decompressing payload data, then verify the
   domain-separated container signature over that digest;
6. recheck source metadata, acquire the identity lock and recover an
   interrupted transaction;
7. create the private `incoming` slot and write the logical Manifest and
   Package signature;
8. seek to the payload and decode it sequentially, hashing each decoded file
   while writing it to `incoming`;
9. require every size and SHA-256 digest to equal the signed Manifest and
   recheck source metadata;
10. apply durability barriers and use the existing slot transaction to make
    `incoming` current;
11. remove the staged `.pxa` after a successful commit according to Inbox
    retention policy.

No Artifact is selected or loaded from the `.pxa` file. Runtime activation
continues to use the verified installed directory. Product-owned built-in Apps
may remain directory Packages and need not store duplicate `.pxa` files in the
system assets tree.

If power fails before activation, recovery keeps the old current directory and
discards the incomplete `incoming` directory. If power fails after
activation but before Inbox cleanup, the new directory remains current and
the authenticated staged container may be cleaned up idempotently.

Production online-update policy deletes an installed staged container by
default; the authenticated update catalog provides idempotence. A development
Inbox may retain it for repeated tests, but retained containers count against
the same LittleFS capacity and must never be treated as the active Package.

## Resource bounds and space accounting

Before authentication, Hosts apply configured hard limits to container size,
payload size, decoded size and file count. The ESP product profile retains its
current maximum of 32 files even though the portable Package limit is 128.

For a staged full update, peak logical storage is approximately:

```text
current_unpacked + staged_container + incoming_unpacked + filesystem_overhead
```

The installer must reserve additional LittleFS garbage-collection and metadata
headroom rather than treating `unpacked_size` as an exact physical-space
prediction. After commit, it removes the staged container and previous slot.

The decoder adds one 4096-byte compressed-input buffer to the existing
4096-byte installer output buffer. Manifest parsing and signature verification
retain their existing independently bounded workspaces.

## Update ordering profile

The existing Manifest `version` is display text and has no defined ordering.
A production online-update channel needs a signed monotonic value that remains
available after the container is deleted. Package Manifest 0.2
adds this required top-level record before Component records:

```text
tag 9 | length 8 | release_sequence:u64
```

Manifest 0.2 requires `release_sequence > 0`. A Host supporting 0.2 applies
these policies for the same verified management identity (the publisher
lineage root and App ID when a lineage is present):

- a greater sequence is an update;
- an equal sequence is idempotent only for the exact same Manifest bytes;
- an equal sequence with different Manifest bytes is a replacement and
  requires explicit confirmation;
- a lower sequence is a valid downgrade, but automatic update must not select
  it and an interactive installer must show the current and staged versions
  and sequences, warn that newer private data may be incompatible with older
  code, and obtain explicit confirmation;
- a Manifest 0.1 Package is unordered legacy input and requires the same
  explicit downgrade confirmation before replacing an installed Manifest 0.2
  Package.

Downgrade policy does not bypass Package validity, Host compatibility,
publisher lineage or revoked-key checks. Private data remains attached to the
same verified App lineage unless the user separately chooses to clear it.

Because the sequence is inside the logical signed Manifest, it survives
extraction and transaction recovery without a second NVS commit. Package
Manifest 0.2 requires a parser update and is intentionally not retrofitted as
an optional 0.1 record.

An update catalog is a separate channel object. At minimum, one entry contains
the publisher key ID, App ID, release sequence, target profile, URL, exact
container size and SHA-256 of the `.pxa`. TLS and catalog authentication protect
discovery and resumption; the two publisher signatures remain the authority
for installation.

## Publisher key rotation profile

Different ECDSA signature bytes do not by themselves mean that a key changed.
A rebuilt Package is normal when verification still succeeds with the same
public key. Renewing or replacing a certificate wrapper while retaining the
same canonical SubjectPublicKeyInfo also leaves `publisher_key_id` unchanged.
The rotation rules below apply only when the public key and therefore
`publisher_key_id` change.

Changing the Manifest `publisher_key_id` normally creates a different App
identity and cannot inherit private data. A planned key rotation uses a signed
publisher lineage carried by Package Manifest 0.2. It adds this zero-or-one
top-level record after `release_sequence` and before Component records:

```text
tag 10 | length N | publisher_lineage:PXKL
```

With no lineage record, the lineage root and current signer are both the
Manifest `publisher_key_id`. With a valid lineage, managed identity becomes:

```text
(lineage_root_key_id, app_id)
```

The Manifest `publisher_key_id` remains the current signer. Private data,
permission policy and update ownership use the managed lineage identity, so a
valid rotation changes the signing key without changing App ownership.

### PXKL encoding

The lineage envelope starts with:

```text
offset  size  field
0       4     magic = "PXKL"
4       2     major = 0
6       2     minor = 1
8       2     header_size = 16
10      2     link_count
12      4     body_size
16      N     consecutive links
```

There are at most eight links. `body_size` is the exact size of all links and
the lineage record has no trailing bytes. The first old key is the lineage
root. Every following link's old key is the preceding link's new key, and
generation starts at one and increases by one.

Each link is an 80-byte header followed by the new canonical DER
SubjectPublicKeyInfo and a fixed 64-byte signature:

```text
offset  size  field
0       4     generation
4       4     flags
8       32    old_key_id
40      32    new_key_id
72      2     new_spki_size
74      2     signature_size = 64
76      4     reserved = 0
80      M     new_spki_der
80 + M  64    old_key_signature
```

`new_spki_size` is nonzero and at most 160 bytes. `new_key_id` must be SHA-256
of the exact canonical DER bytes. Draft 0.1 defines one link flag:

```text
bit 0  revoke-old-signer
```

Unknown flag bits reject the lineage. Production tooling sets
`revoke-old-signer` by default. Clearing it explicitly permits a later
interactive rollback to that old signer; it never permits silent automatic
rollback.

The old key signs this exact message using the Package P-256 P1363 low-S
algorithm:

```text
ASCII "PXA-PUBLISHER-KEY-ROTATION"
one zero byte
lineage_root_key_id:32
app_id_size:u16
exact app_id bytes
generation:u32
flags:u32
old_key_id:32
new_key_id:32
new_spki_size:u16
exact new_spki_der bytes
```

The root key must already be trusted by product policy or match the installed
App's accepted lineage root. A valid link authorizes its new key only for the
named App ID and lineage; it does not add that key to the global publisher
trust store or authorize it to sign other Apps.

For an existing App, a forward rotation must contain the installed lineage as
an exact prefix. A Host rejects a different branch from any earlier key. A
signer rollback uses a shorter prefix and is governed only by the explicit
rollback rules below. Product policy retains public verification material for
accepted lineage roots; retaining a public key for chain verification does not
re-authorize that key to sign after a revoking link is committed.

The final link's `new_key_id` must equal the Manifest publisher key ID. The
logical Package and container signatures are both made by that final key. The
normal new-key signatures prove possession of the corresponding private key,
while the link signatures prove continuity from the old publisher.

The complete lineage is retained inside the installed signed Manifest. A Host
can therefore reconstruct identity and revoked signer state after reboot or
transaction recovery without a separate NVS commit. Each subsequent rotation
includes the full chain from its trusted root.

### Rotation and downgrade policy

A version downgrade signed by the currently accepted signer is allowed after
the normal downgrade warning and confirmation. A package whose final signer is
an older key in the lineage is handled separately:

- if a later committed link revoked that signer, ordinary installation rejects
  it even when its release sequence is lower;
- if it was not revoked, an interactive installer may accept it only after a
  stronger signer-rollback warning;
- automatic update never moves backward in signer generation.

This distinction keeps ordinary version rollback available without undoing a
security-motivated key rotation.

A package with a different publisher key and no valid lineage is not an update.
The installer reports a publisher mismatch and does not expose the old App's
private data. Product UI may offer uninstalling the old identity and installing
the package as a new identity, but data retention or deletion remains a
separate explicit choice.

A lost old private key cannot produce a valid continuity link. Treating an
unrelated new signature as the same App would let any attacker claim its data.
Continuity then requires a separately pre-provisioned product recovery
authority and explicit recovery UI; without one, the new signer is a new App
identity and does not inherit private data.

If the old key is believed compromised, an old-key-only transition is not
sufficient evidence of recovery. Product policy must revoke it through a
trusted recovery or firmware channel, and the replacement lineage must be
authorized by that channel. The basic PXKL chain is for planned rotation, not
post-compromise recovery.

## Delivery variants

Publishing tools may build a universal source bundle containing portable Wasm,
several AOT targets and alternative resources. A device `.pxa` is instead a
complete signed variant for one advertised Host profile. For example, a
publisher may issue separate ESP32-S3 AOT, ESP32-S3 AOT-plus-Wasm-fallback and
simulator variants.

Every variant has its own canonical Manifest and both publisher signatures.
An update server selects an already signed variant; it does not remove files,
recompress or resign publisher content. This avoids sending unrelated
architectures while keeping the delivery service outside the publisher trust
boundary.

## Deferred delta format

Draft 0.1 `.pxa` containers are always complete and independently installable.
They contain no copy-from-current records, binary patches or installation
scripts.

A future delta transport should use a distinct version or extension and must:

- name the exact base Manifest SHA-256 and target Manifest;
- fall back to a full `.pxa` when the base is absent or different;
- authenticate patch instructions before applying them;
- construct a complete private `incoming` directory;
- validate every final target file against the target Manifest;
- use the same atomic commit and recovery state machine.

File-level reuse should precede binary patching. PXA Artifacts are currently
small, while a binary patch engine adds significant parser, memory and test
surface.

## Excluded features

The container deliberately has no:

- symlinks, hard links, devices, ownership or mode metadata;
- absolute or repeated paths;
- executable pre-install, post-install, update or uninstall scripts;
- encryption or DRM claim;
- direct-execution or mounted-archive ABI;
- implicit dependency resolution between Apps;
- server-authorized replacement of the publisher signature.

Package data migration and optional feature modules require separate signed
protocols. Publisher key rotation uses the signed Manifest 0.2 PXKL profile;
none of these mechanisms are smuggled through container flags or executable
installer hooks.

## Required validation

Before implementation is considered compatible, golden and malformed vectors
must cover at least:

- one stored payload and one mixed raw/LZ4 payload;
- empty and exact-4096-byte files;
- every fixed header field and checked-size overflow;
- CRC, logical signature and container signature failures;
- publisher identity mismatch between all three signed structures;
- truncated, duplicate, reordered and trailing file records;
- invalid LZ4 data and every chunk-size relation;
- decoded file size and digest mismatch;
- mutable-source detection;
- every installer power-loss checkpoint before and after activation.
