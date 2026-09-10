# LVGL Backend

This directory contains LVGL-specific helpers that are outside the portable
system and renderer contracts. `page_manager` is the shared page-stack and
cross-thread event queue used by legacy LVGL applications.

The parent firmware retains `components/page_manager` as an ESP-IDF
compatibility component. New standalone or product CMake integrations should
use this directory directly and link the `page_manager` target (or
`pxsys::page_manager` when it is enabled from the PXA System top-level build).

Standard system roles belong in `../../reference`; product pages and hardware
callbacks must stay outside this backend library.
