# PXA System LVGL Renderer

LVGL implementation of the backend-neutral renderer provider. It owns logical
surface roots, visibility, ordering and semantic theme application. The core
system sees only `pxsys_renderer_provider_t`; LVGL types are confined to this
component's backend-specific extension header.

Products can supply an optional transaction decoder. Without one, the renderer
supports native-root surfaces and empty synchronization transactions, and
rejects non-empty portable transactions as unsupported.

All calls run on the LVGL owner thread while the product's LVGL lock is held.
