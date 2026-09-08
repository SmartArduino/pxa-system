# Compatibility policy

PXA services are versioned independently. Core, Package and services other
than UI remain governed by `spec/draft-0.1`; UI service 0.3 is governed by
`spec/draft-0.2`. A `libpxa` implementation change
must not alter either wire format without a corresponding service-version
change and golden-vector update.

All PXA-owned public versions use `0.minor.patch` until their contracts are
stable. During this period an incompatible contract change increments
`minor`, while compatible fixes increment `patch`. Wire capability ranges
carry only `major` and `minor`; patch is a source and release identifier and
does not affect negotiation. The Wasm import namespace follows the ABI major
and is therefore `pxa.core.v0` throughout the 0.x series.

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
