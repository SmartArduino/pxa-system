# Package resources: PXR1 1.0 files and PXRI 1.3 catalog

The implementation includes the compiler, catalog validator, bounded loader,
immutable raster storage, budget cache, POSIX/ESP workers, Core v1 Assets
service, Guest C SDK, and desktop/ESP runtime integration.
These file formats do not change the existing upload packet or DrawList encoding.

Assets 2.0 removes the old READ block-boundary rule. Recompile Guests with the
current SDK and repackage them to request service major 2. The Core wire
envelope remains v1; PXRI file versioning is independent of the service ABI.

## Packaging and trust

`package_app.sh` copies the selected target asset directory, runs
`compile_resources.py`, then builds the signed manifest and container. An
optional `resources.json` beside `package.json` describes offline compilation:

```json
{
  "assets": [
    {"path": "assets/tiles.pxr", "kind": "index8", "source": "resources/tiles.bin", "width": 256, "height": 256},
    {"path": "assets/colors.pxr", "kind": "palette", "source": "resources/colors.json"}
  ]
}
```

`index8` takes exactly width × height bytes. `palette` takes a JSON array of
256..65536 RGB565 integers in complete rows of 256. For PNG or another
Pillow-readable source image, `kind: "texture"` uses `source` and `palette`
fields instead of dimensions; `palette` names the shared RGB565 JSON file.
Image conversion reserves index zero for transparent texels and maps opaque
colors deterministically to the nearest entry 1..255 without dithering. It
rejects partial alpha; an explicit INDEX8 coverage atlas retains all 256 alpha
levels for the renderer's coverage sprite path. Raw INDEX8 import preserves
indices exactly. All textures are at most 256 × 256.

Outputs must be distinct `.pxr` paths under `assets/` and may not overwrite
copied files. Sources must stay under the app directory. Symlinks and traversal
are rejected. Keep source images outside `assets/` when they should not also
ship as UI images. Pillow is needed only for image conversion.

The generated `assets/resources.pxi` indexes package assets and is itself
included in the signed manifest's file inventory. It does not index itself.
Whole-file digests remain in the manifest and are verified at installation.
Runtime trusts the installed package; it does not rehash the index, resident
assets or stream contents, nor defend against post-install file modifications.
The catalog still checks structure, canonical paths, formats, lengths and
manifest membership using `pxa_package_file_find`.

## PXR1 renderer file

All integers are little endian. There is no compression in version 1.0 and
there are no implicit struct layouts or native pointers. Header length is 32:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | 4 bytes | `PXR1` |
| 4 | u16 | major = 1 |
| 6 | u16 | minor = 0 |
| 8 | u16 | kind: texture = 1, palette = 2, UI image = 4 |
| 10 | u16 | encoding: INDEX8 = 1, RGB565 = 2, straight BGRA8888 = 7, premultiplied BGRA8888 = 8 |
| 12 | u16 | width |
| 14 | u16 | height / palette row count |
| 16 | u32 | payload / decoded bytes |
| 20 | u32 | payload offset = 32 |
| 24 | u64 | reserved = 0 |

Texture size must be exactly width × height, each dimension 1..256. Palette
width is 256 and row count 1..256; size is width × height × 2. The actual file
size must equal header plus payload, without trailing bytes. Future format
versions are explicitly rejected. Palette bytes are converted to native RGB565
once, after reading the file and before publishing the immutable object.

UI images have dimensions 1..4096, and the payload must be exactly width ×
height × 2 for RGB565 or width × height × 4 for either BGRA8888 encoding.
Encoding 7 contains straight alpha. Encoding 8 contains BGRA channels already
multiplied by alpha, matching LVGL's ARGB8888_PREMULTIPLIED source format;
each color channel must be no greater than alpha. Resource compilation validates
raw encoding-8 input and records the format in the catalog. The Host loads
both encodings directly into their final aligned image object, with no draw-time
pixel conversion. A consumer must interpret the alpha mode from the encoding,
not merely from the four-byte pixel size.

## PXRI index

The index header is 16 bytes: `PXRI`, major:u16 = 1, minor:u16 = 3,
entry_count:u32, total_bytes:u32. Each record has a 24-byte fixed prefix,
followed by its path bytes, then zero padding to a multiple of four bytes:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u16 | path byte count |
| 2 | u8 | kind |
| 3 | u8 | encoding |
| 4 | u16 | width |
| 6 | u16 | height |
| 8 | u32 | stored file bytes (includes PXR1 header) |
| 12 | u32 | decoded payload bytes |
| 16 | u16 | resource format version (PXR1 = 1, other files = 0) |
| 18 | u16 | reserved = 0 |
| 20 | u32 | payload offset (PXR1 = 32, other files = 0) |

Paths are strictly increasing, unique ASCII package paths of at most 255
bytes, begin with `assets/`, and contain only letters, digits, `._-/`.
Empty segments, `.` and `..` segments, NULs and backslashes are forbidden.
The total count/length, padding, dimensions, encoding, decoded size, offset and
signed file size are validated before the catalog becomes visible.

PXRI 1.3 records end after their padded path. There are no block digest tables.
The compiler emits 1.3; the parser can skip the legacy tables in 1.1/1.2 without
using them. Earlier Hosts reject minor 3 instead of misinterpreting it. Unknown
major/minor versions remain unsupported; PXR1 remains 1.0.

The native `block_map`/`block` helpers now describe bounded byte ranges, not
integrity blocks: they retain installed metadata, compute at most 4096 bytes
starting at the requested offset, and perform no hashing or aligned over-read.
The catalog and manifest must outlive all consumers. Guest READ still accepts
only raw blobs; music accepts indexed Ogg streams.

`pxa_asset_stream` reads directly into the decoder's output argument. It has no
private verification buffer, no block cache and no intermediate memcpy.
Absolute seek including EOF is supported; I/O and cancellation errors remain
sticky. After an error the caller discards partial buffer contents. Desktop
Vorbis/Opus callbacks and the ESP decoder use this same bounded helper; their
catalog references survive queued commands, replacement and activation exit.
PCM rings, codec input/output and native state still count toward total memory.

The POSIX resource worker starts its task only before accepting its first
asynchronous LOAD/PREFETCH/READ. Catalog-only music uses the existing decoder
task without an additional asset-worker stack. `pxa_posix_asset_worker_stack_bytes`
reports zero until that first task starts, then its configured stack size;
pthread runtime overhead and actual high-water use are separate measurements.

Additional kinds are audio = 3, image = 4, blob = 5. Encodings are raw = 0,
Ogg Opus = 3, Ogg Vorbis = 4, PCM U8 16 kHz mono = 5, PNG = 6. Streaming Ogg
has decoded size zero: this forbids interpreting the field as a whole-track PCM
allocation. PNG reports logical RGBA byte size width × height × 4; decoder
workspace, stride padding and converted representations must be budgeted
separately by the consuming backend. PCM effects of 1..16000 bytes use the
existing audio format; other `.pcm` files remain blobs rather than advertising
unsupported playback. Other package assets are discoverable as raw blobs.
An index entry does not imply that every consumer supports its encoding.

## Host loading contract

`pxa_asset_catalog_init/find` borrow immutable index and manifest storage and
perform no allocations or file I/O. The owning package must outlive catalog
users. `pxa_raster_asset_required_bytes` computes the exact object plus payload
allocation to reserve in a resource budget before loading.

`pxa_asset_load_raster` is worker-only. It checks a 32-byte stack header against
the catalog, allocates the final pixel object once, and reads chunks no larger
than 4096 bytes directly into that object. It supports short reads, cancellation
between chunks, and optional scheduling yields. It checks EOF and the declared
length before publication. All failures release the partial object and return
a null result. The caller owns the input stream and closes it on every outcome;
the caller must also recheck request lifetime when publishing a worker result.

Assets 2.0 also loads short PCM through `pxa_asset_load_resident`, using the
same worker and cache. `pxa_asset_object_required_bytes` accepts either a
validated raster description or audio kind 3 / PCM encoding 5 with zero format
version, offset and dimensions, and equal stored/decoded sizes of 1..16000.
PCM is headerless: read directly into one final allocation, check EOF, then
publish. Failed, cancelled or oversized reads
never yield a usable handle. The raster-only wrapper still rejects audio.
STATUS and PREFETCH support the same resident PCM encoding; Ogg remains a
stream consumer rather than a resident PCM LOAD.

`pxa_posix_asset_open` opens each path component relative to directory file
descriptors with `O_NOFOLLOW`; final files must be regular and have the signed
size. It rejects symlink directories, leaf symlinks, FIFO/device files and
traversal, without a `realpath`-then-`fopen` race. Runtime does not compute a digest. Package roots are trusted Host configuration.

`pxa_asset_catalog_blob_block` returns a bounded range starting at the exact
requested offset. EOF produces zero bytes, and offsets past EOF are rejected.
`pxa_asset_load_blob_block` reads directly into final result storage, supports
short physical reads and cancellation, and publishes only a completed result.
POSIX `pxa_posix_asset_open_range` checks the installed file length and positions
the descriptor for that range. There is no integrity block or SHA operation.

Assets 2.0 connects these primitives to Guest READ, the SDK result view and
the existing POSIX/ESP resource workers. A fixed Host-profile read queue shares
the texture worker; no additional task is created. Reads and raster jobs
alternate when both classes are ready; running file operations are not
preempted. Released result blocks are freed by the worker outside queue locks,
and slot/budget capacity is returned only after actual release. All read jobs
must drain before package, catalog or allocator storage is retired.

Immutable objects, bindings and frame snapshots reuse `raster_assets.h`.
Eviction must check both external pins and object references; an in-flight
frame can keep data alive after the Guest closes its handle. Allocation and
last-reference frees must run outside render critical sections. This loader
alone does not enforce global or app quotas: the cache reserves allocations
and the worker executes its load/release jobs. Product Hosts also use the shared
`resource_budget.h` allocator for file/dynamic raster objects, compressed UI
data and short PCM. ESP additionally charges music buffers, audited codec
hooks and audio device/RTOS storage. Actual UI decoded buffers, desktop codec
internals and other platform allocations still need coverage or separate full
accounting; the cache quota is not a total subsystem memory cap.

`pxa_memory_budget_config_t.temporary_limit[2]` adds an explicit global ceiling
for TEMPORARY allocations in each memory class. Hosts must initialize it;
zero prohibits temporary allocation. The allocation must also fit its class's
total global and owner limits. The ceiling is not a reservation: resident
objects can still cause total-quota failure. Prefix bytes, pending native
allocations, old+new resize storage and retiring owners all count until actual
free returns. A temporary-only rejection does not request raster eviction,
which cannot restore temporary capacity. `temporary_peak[2]` reports this
category's high-water mark globally and per owner. This Host API does not alter
the Guest wire format or bring uncaptured codec/driver allocations into budget.

## Async service and ownership

Assets service 21 version 2.0 requires Core v1. QUERY (1), LOAD (2), PREFETCH (3)
and STATUS (4) use `path_length:u16 | kind:u8 | reserved:u8 | path[path_length]`.
QUERY/STATUS require kind zero; LOAD/PREFETCH accept texture (1), palette (2),
and resident audio (3, PCM U8 16 kHz mono only). Paths use the catalog
rules above. No NUL terminator is serialized. The runtime backend resolves
only the authenticated package belonging to the calling component.

READ (5) uses `path_length:u16 | reserved:2 zero bytes | offset:u32 |
max_bytes:u32 | path[path_length]`. It accepts indexed raw blob kind 5; other kinds are unsupported. The
request length is 1..4064, fitting a 4096-byte Core v1 message after the 20-byte
envelope, 4-byte completion status and 8-byte read-result header. Success is
`status:i32 = 0 | offset:u32 | total_bytes:u32 | data:bytes`; failures contain
only status. The actual data length is the minimum of the requested length and
the remaining file bytes. Exact EOF succeeds with zero data; offsets beyond EOF fail. Advance
by the actual count, not by the requested count. No resource handle is created.

READ reserves completion capacity before accepting backend work. The worker
allocates only the requested result bytes plus its 8-byte header through the
shared TEMPORARY allocator, and reads directly into that allocation. The runtime
copies the result into the reserved event and releases the worker ticket; Guest
memory never crosses onto the worker. The SDK view is borrowed only during the
event callback. Worker result, Core event pool and Guest receive buffer can
overlap and are reported separately. Four retained jobs is the current Host
profile, not a universal wire guarantee; earlier quota/event rejection is valid.

Sequential READ no longer rereads aligned blocks or shifts a slice inside a
larger verification buffer. The sample's 8193-byte map needs exactly 8193 source
payload bytes, rather than 16385, with maximum-size requests. This is logical
file I/O, not a measurement of filesystem/device cache traffic.

The 20-byte info record is `kind:u8 | encoding:u8 | format_version:u16 |
width:u16 | height:u16 | stored_bytes:u32 | decoded_bytes:u32 |
resident_bytes:u32`. QUERY success returns info; LOAD success returns
`handle:u64 | info`. All completion payloads start with the Core `status:i32`;
errors have no further payload. Resident bytes estimates the final raster
allocation on this Host and excludes fixed metadata, stacks, and decoder
workspace; it is zero for non-raster entries, not a free-decoding guarantee.

Submission rejection produces no completion. Acceptance reserves completion
storage before queuing the backend operation. LOAD remains cancellable while
queued/loading. `pxa_assets_service_poll` runs on the runtime thread and
publishes exactly one result or observes a Core cancellation. The worker only
signals the runtime. A Host-only monotonic Core request identity prevents a
late completion from attaching to a reused request ID, even for another
service. Identity exhaustion rejects further requests instead of wrapping.

PREFETCH follows the same acceptance/cancellation/result contract, but success
returns the 20-byte info record without a handle. Completion releases its cache
ticket; the resulting object is evictable immediately. Before binding, LOAD
must acquire a real handle even after successful prefetch. Duplicate foreground
and prefetch requests share one job but cancel independently. Queued jobs with
any foreground waiter run before prefetch-only jobs, then FIFO within a class;
cancelling the last foreground waiter demotes the job again. Running reads are
not preempted, and a full queue still returns backpressure. Prefetch does not
create a second worker or an unbounded reservation pool.

STATUS returns `state:u8 | reserved:3 zero bytes | load_status:i32` after the
Core status prefix. States are ABSENT=0, QUEUED=1, LOADING=2, READY=3 and FAILED=4.
`load_status` describes a cached failure only. Missing catalog paths produce
NOT_FOUND; an indexed raster that is not cached returns a successful ABSENT
snapshot. Non-raster paths are UNSUPPORTED for this operation. The lookup uses
the same package activation, component owner, path, stored manifest digest and representation key as
loading. It performs no I/O, creates no ticket, and does not touch LRU order.
READY can be evicted after the snapshot; completion events remain the normal
way to observe individual requests, including cancellation. A cancelled
request is not a cancelled shared cache entry.

Guest C entry points are `pxa_assets_prefetch_texture/palette`,
`pxa_assets_status`, and `pxa_assets_parse_status`. Parse prefetch completion
with `pxa_assets_parse_result(..., PXA_ASSETS_PREFETCH, ...)`; it transfers
no handle. `pxa_cancel(token)` cancels either loading or prefetch. Declare
Assets 2.0 (current package tooling selects 2.0).
The `resource-scenes` app exercises STATUS at startup and optional prefetch
between scenes, and still uses LOAD before binding. Under low budgets it
accepts optional prefetch failure and retires the old scene before loading.
`prefetch_failures` is the subset of `load_failures` with no foreground waiter
at failure; reports keep both, rather than hiding warming failures.

The successful handle owns a cache pin and a raster reference. Core close
releases both; bindings and in-flight frames retain their own references.
Cache release schedules final reclamation on its worker. The backend, verified
catalog and package storage must outlive all such consumers. Core component
stop releases pending tickets as well as open handles. Queries and status
checks do no file I/O. Cache/worker tickets are never Guest handles.

`pxa_asset_scene.h` is a Guest-only convenience layer over these operations,
not a new service/version or worker queue. It prevalidates item descriptions,
submits a bounded window of LOAD requests using an app-reserved contiguous
64-bit token range, and gathers handles for the existing atomic batch binding.
It uses fixed caller storage and the app's event dispatcher. All successful
items remain held until binding/release; the Host resource-handle quota applies
to the whole group, independently of its in-flight window. On failure/release,
accepted requests are cancelled and their results must drain; queued successes
are closed even if cancellation loses the race. The first failure is retained.
New begin cannot overwrite undrained requests or unclosed handles. Release
leaves independent render bindings/frames intact. At component stop Core revokes
resources; Guest imports are forbidden in the subsequent stop callback. See
the [SDK guide](../../sdk/guest-c/README.md#file-textures-and-scene-resources)
for storage lifetimes and concrete API usage.

`pxa_assets.h` provides `pxa_assets_query`, `load_texture`, `load_palette`,
`build` for reusable control buffers, and `parse_result`. Core `pxa_cancel`
and `pxa_close_handle` handle cancellation and release. The convenience
load call uses a bounded stack packet, never a pixel or decode buffer.

GameRender 0.5 adds direct I/O operation `0x103`, feature bit 0
(`asset-bindings`). The payload is `count:u16 | reserved:u16`, followed by
1..49 entries of `kind:u8 | slot:u8 | reserved:u16 | handle:u64`. Texture
slots are 0..47; the palette slot is zero. Duplicate destinations and nonzero
reserved fields are rejected. Handle zero explicitly unbinds. The whole
batch is validated for type, full generation and component ownership before
calling the backend; any failure leaves every prior binding unchanged.
`pxa_game_render_bind_assets` supplies the convenience API. Closing a
Guest handle after a successful bind does not invalidate that binding.

DrawList validation also computes Host-only texture and palette dependencies.
Pending/executing frames retain only those dependencies; a clear-only frame
does not pin any assets. Solid sprites retain their coverage texture but not
the palette. Solid painter variants retain a palette only when their execution
kernel uses palette indices. Bindings themselves remain owning references
until explicitly replaced/unbound or their context closes. ESP takes a full
snapshot for validation outside its binding lock, then prunes unused references
outside the lock before publishing the frame. Desktop snapshots dependencies
directly. These Host-only fields do not change the Guest DrawList wire format.

The current desktop profile defaults to a 512 KiB external raster-object
budget, 64 KiB internal budget, 64 entries, 48 tickets, and 16 queued loads.
`PXA_ASSET_EXTERNAL_BYTES`, `PXA_ASSET_INTERNAL_BYTES` and
`PXA_ASSET_READ_DELAY_US` provide bounded test overrides. Worker stack and
metadata are reported separately. These are desktop test defaults, not
a complete application memory cap. Both product profiles use 64 entries to
accommodate a 48-texture renderer plus a palette without expanding byte budgets.
Desktop allocates the worker and service only when a component declares Assets.

The ESP product Host now registers Assets for components declaring it. One
persistent FreeRTOS worker is reused across sequential package activations;
it signals permanent Host state, never an application arena. Defaults are
512 KiB PSRAM objects, 64 KiB internal objects, a 64 KiB maximum index and a
6144-byte internal task stack (Kconfig can change object budgets/stack). Cache
metadata and index use PSRAM. No pixel allocation falls back to an uncharged
memory class. Between at-most-4096-byte reads the worker checks cancellation
and a 2 ms work slice, yielding a tick after that slice; higher-priority audio
tasks may preempt it. This does not bound the filesystem's own blocking time.

ESP input assumes an immutable Host-owned LittleFS package root, without
symlinks or device files. It checks normalized indexed paths, regular-file
size before publishing pixels, without runtime SHA or a hash context. This is
not the POSIX adapter's component-by-component symlink protection contract.
Shutdown stops music, cancels requests and drains music input leases, worker
jobs and frame references before
freeing catalog/cache metadata and the activation arena. The fixed sleeping
task and static mutex remain accounted after activation ends. Reported task
stack is configured capacity; RTOS TCB, crypto/filesystem internal allocation
and allocator overhead still require separate instrumentation.

## Verification

- `pxa_asset_catalog_test`: metadata/path/version/size checks, short reads,
  bounded chunk sizes, cancellation, failed allocation, truncation, trailing
  bytes, I/O failure and pixel lifetime.
- `pxa_resource_compiler_test`: deterministic index, raw/palette/image
  compilation, traversal, symlink and malformed-file rejection.
- `pxa_asset_input_test` (POSIX adapters enabled): Python compiler → real files
  → installed catalog → C raster loader → checked output pixels,
  cancellation, symlink files/directories, FIFO and missing files.
- `tools/package/test_package_tool.sh`: generated resource index included in
  the signed inventory and verified with the existing package signature path.
- `pxa_asset_cache_test`: quotas, independent duplicate cancellation,
  in-flight references, real-free accounting, and 100 scene transitions.
- `pxa_asset_worker_test`: real file loading, gated slow I/O, malformed headers,
  cancellation/retry, and shutdown while a read is in flight.
- `pxa_assets_service_test`: native SDK transport, full-width handles, request
  ID reuse after cancellation, cross-component rejection, atomic binding,
  old-frame ownership, explicit unbind and component shutdown.
- `pxsys_resources_test PACKAGE_DIR PUBLISHER_KEY_DER`: actual signed
  resource-scenes AOT through WAMR and the desktop loop, with slow reads,
  checked texture pixels over 100 scenes and normal teardown. Build the app
  before running this integration target; it is not an implicit core test.
- Platform repository `firmware/components/pxa/tests/test_host.sh` includes the
  actual ESP worker running with pthread FreeRTOS stubs, real mbedTLS and files:
  100 scenes, read/cancel/retry, malformed-file failure/retry, frame-held shutdown and
  reuse of one task across activations. Requires system mbedTLS development
  headers/library. Shared ESP/desktop renderer tests check unused frame
  references can be reclaimed before presentation without changing pixels.
