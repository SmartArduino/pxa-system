> Historical Core 1 notes. Current code uses [Core 2](abi-2.0-migration.md);
> old import compatibility and the optional direct-call macro are removed.

# PXA Core 1.x implementation and migration

Core 1.0 uses a 20-byte little-endian envelope, opaque 64-bit request tokens and
resource Handles, `pxa_submit` and typed `pxa_io`. All current package imports
use `pxa.core.v1`; no Core v0 import binding remains. Independently versioned
services retain their own schemas and feature negotiation.

Core 1.1 adds optional `pxa_call(route, token, payload, length)` with the same
control/completion contract. It accepts borrowed payload bytes without an
encoded header or per-call copy. `route = service | (opcode << 16)`; payloads
remain bounded to 4076 bytes. A package using it declares `min_sdk >= [1,1]`.
Import names, kinds and required Core minor are checked before signing and by
the WAMR adapter during activation. Unknown Core functions are rejected.

Host dispatch and mailbox events now use the same envelope as the Guest.
`pxa-core.json` generates both codec headers and fixes golden bytes. The
12-byte Host envelope, second schema/header and WAMR's 16-slot token map were
removed. Service handlers own operation/payload validation; the adapter checks
memory, lifecycle, import capabilities and pinned-memory use.

A request's token is reserved until its completion is taken from the mailbox.
An immediate rejection has no backend effect and no later completion. Queued
completion cancellation preserves the original result. Duplicate tokens return
`busy`, including across services. Capacity is selected through Runtime limits,
not a fixed adapter table. Completion storage is reserved before acceptance.
Shutdown cancels requests, releases resources and clears queued events safely.

The native Host API uses `pxa_request_token_t` (`uint64_t`), `request_token`
fields and the canonical `pxa_message_decode` / `pxa_writer_message` functions.
Host clients must rebuild and update old request callback types. Guest 1.0 wire
bytes and installed binaries do not change. Work IDs, IPC call IDs, UI node IDs
and texture IDs remain their separately defined identifiers; widening these
would add cost without resource-generation safety.

Guest resource Handles stay opaque, typed by Host ownership and generation.
The native embedding library still has internal 32-bit resource helpers used by
native tests; they are not a supported Wasm dialect. The authoritative Core 1.x
service specs and golden vectors describe 64-bit resource fields. Handle closing,
stale generations, cross-Component access and permission revocation are tested.

## C++ SDK

`PXA_CORE_DIRECT_CALL=1` builds Transport with borrowed control payloads and no
512-byte internal scratch. It is propagated to the SDK library and every CMake
consumer to keep class layout consistent. Packaging enables it for C++ CMake
packages whose minimum Core is 1.1; minimum Core 1.0 keeps submit behavior.
`send_prebuilt` borrows the payload after the reserved header instead of filling
that header. Typed Handle I/O and draw batches keep their existing data path.
A direct-only Transport accepts any payload within the Core limit without
requiring a larger caller scratch buffer.

The default request table remains 16 entries, while `PXA_REQUEST_CAPACITY`
allows explicit larger fixed budgets up to the Host's 16-bit capacity domain.
Core tokens are unique across services; notification identifiers with opcodes
>= 0x8000 use a separate namespace. No new heap allocation or default cache is
introduced into the hot path.

Device runtime-info and Window snapshot payload codecs are schema-generated.
Other service payloads are still handwritten and verified with native, Guest,
WAMR and signed-package tests; full service-codec generation remains future work.

## Verification

Run `tools/check_spec.py`, `tools/test_wire_codecs.py`, package import tests,
`tools/package/test_guest_cpp.sh`, libpxa CTest and full desktop CTest. The signed
pressure fixture checks token reuse, duplicate/full-table rejection, reserved
completions and native 64-bit Handle lifetimes across services. Native tests also
exercise 32 concurrent tokens with identical low halves, insufficient event
buffers, stale consumption and stop cleanup. The optional ABI control benchmark
measures import+dispatch; it is not a game FPS benchmark.

See [the 2026-10-11 audit](../../docs/zh-CN/abi-audit-20261011.md) for paired
firmware, actual display timing, memory and remaining limitations. Desktop
microbenchmarks do not establish device frame latency or PSRAM/SRAM peaks.
