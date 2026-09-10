# Development Signing Key

This P-256 key signs only the example Packages generated from `apps/pxa`.
Its public key ID is
`17982acd08493944059713aee9a56135dd4a080ecaaf7aa77df11fdf3102d171`.

The private key is intentionally present as development fixture material. It
provides authenticity for tests, not secrecy or production publisher identity.
Production builds must disable `CONFIG_PXA_TRUST_BUNDLED_DEVELOPMENT_KEY` and
provide a product-owned trust hook and protected offline signing key.
