---
name: pxa-app-dev
description: Implement and debug PXA C or C++ applications with a pinned DevKit, local SDK context and an app-only change boundary. Use for PXA apps/games, not SDK or board-port implementation.
---

# PXA application development

The edit boundary is the selected application. SDK, public ABI, host/runtime,
simulator and board sources are read-only, including dependency checkouts.
Start `scope begin --mode app --project apps/name` before editing an existing
app; Agent Kit `init` starts it for a new app. Use `scope check` before delivery.
These checks detect changes and preserve them; they are not a file-access sandbox.
Do not erase violations or rebaseline to hide them. A missing SDK capability is a
separate platform request: explain it and seek an app-level implementation.

Use `context apps/name --query ui --language cpp` or `pxa_context` to get compact
version-matched declarations/examples. Search by actual symbol or module, then
read only the relevant headers. C apps use `--language c`; create them with
`init apps/name --id unique-id --language c`. Both SDKs use the same platform
contracts; do not substitute C++ calls into C source.

During iteration, build incrementally and verify one relevant screen first;
run the requested screen matrix before delivery. Keep the existing lock and
build cache. Do not redownload/reinstall the SDK to fix an app compiler error.
For long MCP jobs, use `pxa_job_status(wait_seconds=10, include_log=false)`
instead of repeatedly emitting the same log; request logs when finished or failed.
Use a reproducible scenario for touch/controller input and inspect its images.
For ordinary retained UI, inspect real nodes and use tap-node plus expect/wait-for
to verify visible state changes; see pxa-debug for the supported semantic actions.
For FPS work, measure CPU, rendering, presentation and memory separately; a
faster simulator is not evidence of a faster physical LCD.

When Agent Kit setup is present, use workspace `.pxa/pxa` (Windows `.pxa/pxa.cmd`) instead of bare `pxa`; MCP tools expose the same implementation. Apps belong in `apps/<name>`. Start with `doctor`; do not rebuild the firmware environment for an app task. Follow the user goal through actual runtime and visual checks, then iterate on feedback.

Read the existing project and `pxa.lock` before choosing the SDK. Use the user's selected DevKit; `pxa --version` and `pxa run --help` reveal its actual capabilities. Check Core ABI, required service versions/features and CPU/AOT compatibility separately from the SDK release number.

Before an authorized device install, build its CPU target and compare the delivered
package with `device compatibility PACKAGE --port PORT` or `pxa_check_compatibility`
when supported by the installed tools. Known incompatibility must stop before
upload. Missing legacy fields remain unknown, not compatible or absent;
`--strict-compatibility` also blocks unknown checks. Do not delete locks or lower
declared requirements to bypass this. `firmware status` reads device/release
metadata without flashing; firmware updates use existing user authorization.

For a new app, `pxa init <directory> --id <unique-id>` generates the working C++26 template. Preserve existing source, identity, signing key and release sequence when editing an app. Do not recreate a populated directory or implicitly update its SDK lock. Work outside the firmware checkout when only building an app.

For a new timer or counter on selected DevKit0.10+, use `recipes` / `pxa_recipes`
then `new apps/name --recipe timer|counter` / `pxa_new`. Timer accepts `--minutes`
and `--break-minutes`; both accept `--title` and an explicit app ID. The recipe
includes a semantic `tests/journey.json`, touch/controller controls and coalesced
checkpoint storage. Run that journey, inspect the actual smallest/round/portrait
captures and adapt it to the user's requirements. Timer completion can be tested
with `--define PXA_RECIPE_TIMER_SECONDS=6` and `tests/complete.json`; rebuild without
that macro for delivery. These are starters, not proof of runtime acceptance.
For a list, device dashboard or game use the locked SDK's virtual-list,
device-sensor or game-input examples via `context`; do not invent unsupported recipe names.

Use `pxa run <project> --offline --debug --watch` for the development loop; it builds incrementally. Do not precede every run with a separate build or compile every CPU for a text/layout edit. Stop watch before verification, check the affected profile first, then the delivery matrix and target packages. Omit `--offline` if downloading the pinned toolchain is needed and authorized. Build first, then verify real Guest readiness, take a screenshot, inject the relevant input and inspect the resulting screen; a successful build is not a working UI. Read the `PXADB: ... --simulator PROFILE@INSTANCE` line emitted by this run and use that exact selector. Stop the CLI you started when finished; it owns its simulator and debug bridge.

For SDK code, read [C++ implementation notes](references/cpp.md) only for the relevant module. Prefer the installed SDK's headers and examples to guessed APIs or historical ABI directory names. A firmware workspace's `tools/app.sh` and `tools/dev.sh` are a separate workflow; they are not shipped in the standalone DevKit.

For multiplayer, remote control or shared state, read
[network transport choices](references/networking.md). Query the actual Host:
browser WebSocket, native TCP/UDP and service discovery are independent capabilities.
For screen rotation, use the Window 0.5 guidance in the C++ notes and verify
the actual dimensions, input and square-screen behavior on each target Host.

For immersive layouts, draw a continuous viewport background and place readable
content and controls inside SafeArea. Keep status/navigation bars aligned with the
user's intended experience. Side Back gesture zones must not shift the whole page;
avoid them only for conflicting edge controls and use actual reported capabilities.
Do not add a fixed left gap or mirror that gap on the right merely to compensate.
Physical cutouts/corners and app design padding are separate constraints; retain
those where needed. An app choosing no gesture avoidance still receives system
Back/Home input; layout policy does not disable system navigation.
See the background and inset guidance in the C++ notes.

If `$pxa-debug` is installed, use its verification helper for screen profiles and scripted interaction. Otherwise use the CLI directly. Keep app service declarations aligned with the APIs actually used, handle `Result` failures, and use optional modules rather than default caches. For games, measure displayed frames, input response and current/peak memory with a reproducible workload before claiming a performance benefit.

Finish with the implemented behavior, commands executed, screenshot evidence and any unverified device requirements. Continue routine edits/builds/testing within the user's scope; installation on hardware, publishing and firmware updates use the authorization already provided by the user.

For a new game/app, first identify the target device with `device-profile` /
`pxa_device_profile`, or ask for its resolution, physical controls and required
sensors if disconnected. Confirm whether multiple screen sizes, orientations,
rounded corners or round screens are delivery requirements. Read actual Window
metrics and Sensor descriptors at runtime; an emulator profile or debug input
support does not prove physical hardware. Keep optional sensors degradable.

For pixel art games, read [pixel game implementation](references/pixel-games.md)
for Z Labs Chinese fonts, integer scaling, layout/input mapping and rendering
choices. Prefer a small complete playable loop before adding content.
