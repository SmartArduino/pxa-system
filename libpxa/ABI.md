# Compatibility policy

PXA services are versioned independently. Core, Package, UI 0.3 and the other
services are governed by the consolidated `spec/draft`. A `libpxa` implementation change
must not alter either wire format without a corresponding service-version
change and golden-vector update.

All PXA-owned public versions use `0.minor.patch` until their contracts are
stable. During this period an incompatible contract change increments
`minor`, while compatible fixes increment `patch`. Wire capability ranges
carry only `major` and `minor`; patch is a source and release identifier and
does not affect negotiation. The Wasm import namespace carries the Core wire
generation: application artifacts import `pxa.core.v1`. Core v0 packages are
rejected at activation. A package declares exactly one required Core major
through `min_sdk`.

The Guest SDK ships a single API surface and its header, function and macro
names carry no version suffix; only the wire ABI above is versioned. The Host
C API in `libpxa/include/pxa` is a separate namespace and keeps its own names.

Manifest 0.5 records a deliberately small Android-style model: signed
`minSdk` is the Core runtime floor, and signed `targetSdk` is the behavior
contract expected by the App. `compileSdk` is build provenance/SBOM metadata,
not a runtime gate. New Host compatibility shims must branch on `targetSdk`,
not a package's release version. The Host does not call a Guest API-version
entry point; signed manifest requirements, service ranges and feature bits
decide whether a capability can be activated.

UI 0.3 intentionally has no source or wire compatibility with UI 0.1. Hosts
must reject a Package whose required UI version range is unsupported; they do
not translate old UI transactions. Optional UI facilities are selected through
the environment feature bits rather than by changing the 0.3 wire layout.

For the `0.x` library series, public C source compatibility is preserved where
practical. Callback/configuration structures that cross ownership boundaries
carry `struct_size` when forward extension is required. Fixed-width integers
are used for protocol values, tokens and status codes.

Native binary ABI is not frozen before `1.0`. Public structure layout and the
set of exported functions may grow between minor `0.x` releases. Applications
that require binary-only upgrades must pin the same minor release.

All public link symbols use the `pxa_` prefix. The library does not export
platform adapter symbols or take ownership of platform callback tables,
encoded manifests, service workspaces or Runtime workspaces.
