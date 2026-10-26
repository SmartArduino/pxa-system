# Third-party notices

The PXA rendering compute kernels and generic PSG implementation are original
PXA code under the repository's MIT license. Game projection, engine-specific
lighting encodings and emulated hardware behavior remain application code.

Image adapters retain their upstream notices in `adapters/image/vendor/`:
`pngle.c` is MIT, and `miniz.c` retains its permissive upstream dedication and
license text. Optional platform adapters link dependencies under their own
licenses; the repository MIT license does not replace those terms.

The system's dependency selections, runtime exceptions and review requirements
are maintained in [the system license policy](../docs/zh-CN/system-license-policy.md).
Distributions using FreeType must include its required attribution and FTL
license. Espressif-specific binary components retain their vendor restrictions.
