# Core 2 migration (exploration branch)

Core 2 exposes only `pxa.core.v2::{pxa_call,pxa_io}`. Submit compatibility is
removed at the signing and activation boundaries. Services and resource formats
remain independently versioned; a Core import upgrade does not select a different
handle payload layout. Every Core 2 Guest uses opaque 64-bit handles/tokens.

Only private test packages are migrated in this round. Production application
sources/manifests remain unchanged; restore the original device firmware after
testing so installed applications remain usable. This is an intentionally
incompatible branch, not a rollout to existing applications.

For a test application:

1. Set `min_sdk`, `target_sdk`, `compile_sdk` to `[2,0]` and use a private package ID.
2. Rebuild with the current SDK and re-sign. An old binary cannot be migrated by
   changing only its manifest, because the imported module name changed.
3. Remove `PXA_CORE_DIRECT_CALL`, `send_direct`, `send_prebuilt`, `PrebuiltPacket`,
   and calls to Transport scratch storage. Use `Transport::send` with payloads.
4. Request encoders write at offset zero and return payload length. Remove the
   former 20-byte request prefix in custom request/IPC encoders. The 20-byte
   Host-to-Guest event envelope is unchanged. Regenerate IPC contract headers.
5. Handle I/O, request cancellation/completion, lifecycle phase checks and
   negotiated service capabilities retain their semantics.

C++ Transport has one implementation and no packet scratch. Owned requests retain
bounded snapshots for coroutine lifetime safety; borrowed overloads preserve
caller ownership. C SDK packet helpers adapt their local
packet builders to call, but there is no Submit import in the Host or SDK.
Native embedding tests may still serialize a message into the internal Host API;
that is not an old Wasm ABI compatibility route.

The optional game-render 0.6 synchronous command mode is a separate experiment,
not a prerequisite for Core 2. ESP and desktop validate and render borrowed commands
without a mailbox; default queued rendering remains available. Both paths retain
Z-buffer, cutout, material validation and framebuffer ownership. Asynchronous
zero-copy of mutable Guest commands is deliberately not exposed: pinning alone
cannot establish immutability. Desktop's single product thread also needs only
one pending command buffer in queued mode; borrowed mode allocates none.

Control payloads remain bounded at 4076 bytes. Requests, handles, resource buffers
and package linear-memory budgets already have explicit configurable capacities.
A larger control-plane input limit must not implicitly enlarge every completion
pool block. No extra default lookup table, cache, queue or worker is introduced.

Historical Core 1 measurements are in `docs/zh-CN/abi-audit-20261011.md`; they
must not be reported as measurements of this Core 2 implementation.

Service admission uses the Host capability profile (ID, version range and feature
bits), not a second fixed list of service IDs in the package loader. An unknown
service is rejected unless the Host explicitly advertises it. This is extensible
without adding a Core import or increasing per-frame lookup storage.
