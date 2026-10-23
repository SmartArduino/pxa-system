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
For FPS work, measure CPU, rendering, presentation and memory separately; a
faster simulator is not evidence of a faster physical LCD.

When Agent Kit setup is present, use workspace `.pxa/pxa` (Windows `.pxa/pxa.cmd`) instead of bare `pxa`; MCP tools expose the same implementation. Apps belong in `apps/<name>`. Start with `doctor`; do not rebuild the firmware environment for an app task. Follow the user goal through actual runtime and visual checks, then iterate on feedback.

Read the existing project and `pxa.lock` before choosing the SDK. Use the user's selected DevKit; `pxa --version` and `pxa run --help` reveal its actual capabilities. PXA 0.5.0 requires Core 2.1; SDK release, Core ABI, service versions and CPU/AOT compatibility are separate concepts.

For a new app, `pxa init <directory> --id <unique-id>` generates the working C++26 template. Preserve existing source, identity, signing key and release sequence when editing an app. Do not recreate a populated directory or implicitly update its SDK lock. Work outside the firmware checkout when only building an app.

Use `pxa build <project> --offline --target simulator` and `pxa run <project> --offline --debug` for the development loop. Omit `--offline` if downloading the pinned toolchain is needed and authorized. Build first, then verify real Guest readiness, take a screenshot, inject the relevant input and inspect the resulting screen; a successful build is not a working UI. Read the `PXADB: ... --simulator PROFILE@INSTANCE` line emitted by this run and use that exact selector. Stop the CLI you started when finished; it owns its simulator and debug bridge.

For SDK code, read [C++ implementation notes](references/cpp.md) only for the relevant module. Prefer the installed SDK's headers and examples to guessed APIs or historical ABI directory names. A firmware workspace's `tools/app.sh` and `tools/dev.sh` are a separate workflow; they are not shipped in the standalone DevKit.

If `$pxa-debug` is installed, use its verification helper for screen profiles and scripted interaction. Otherwise use the CLI directly. Keep app service declarations aligned with the APIs actually used, handle `Result` failures, and use optional modules rather than default caches. For games, measure displayed frames, input response and current/peak memory with a reproducible workload before claiming a performance benefit.

Finish with the implemented behavior, commands executed, screenshot evidence and any unverified device requirements. Continue routine edits/builds/testing within the user's scope; installation on hardware, publishing and firmware updates use the authorization already provided by the user.
