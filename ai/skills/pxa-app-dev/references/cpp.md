# C++26 SDK implementation notes

Paths below are relative to the installed DevKit. SDK source and app templates are included; `sdk/guest-cpp/README.zh-CN.md` documents supported features. C++26 does not imply that every experimental language or standard-library feature is supported by the pinned Clang/libc++ or WASI environment.

- Start from `sdk/templates/hello/main.cpp` and its CMake config. The SDK config sets the checked compiler requirements, O3, ABI definitions and no-exception/no-RTTI options. Do not replace these with a host compiler or a different Wasm toolchain.
- Declarative UI: `pxa/app.hpp`, `ui_component.hpp`, `ui_environment.hpp`, `ui_layout.hpp`, `ui_modifiers.hpp`. `Environment` initializes from app configuration and handles display events; `SafeArea`, dp units and semantic fonts handle DPI and screen geometry. Check layout on both round/high-DPI screens and small rounded screens. Only touch targets that are actually visible count as usable.
- Input/text: `text_input.hpp`, `ui_action.hpp`, `ui_controller.hpp`; request system input instead of implementing another default keyboard. Internationalization: `i18n.hpp`, `i18n_ui.hpp`, `locale.hpp`; use stable message IDs and runtime language changes rather than duplicated view trees.
- Games: inspect `game_service.hpp`, `surface.hpp` and the relevant existing game before choosing a drawing path. Use elapsed monotonic time for simulation; batch drawing, reuse scratch storage and keep allocations out of a per-frame loop. Transparent cutouts need depth semantics. Submission FPS is not display FPS.
- Audio/resources: `audio.hpp`, `assets.hpp`; schedule voices/tracks and preload only the resources needed now. Verify onset timing on hardware when latency is a requirement. Storage/network/work: `storage.hpp`, `fs.hpp`, `net.hpp`, `work.hpp`; use typed handles and scoped ownership, and handle cancellation and permission denial.
- `package.json` declares services, WASI features, permissions, assets and memory limits. Do not fix a missing capability by raising every service to the newest version or inflating all budgets. Inspect the precise build/runtime error first.

For actual firmware/Host changes, read the source integration documentation and the board's build instructions; these notes are about Guest applications.
