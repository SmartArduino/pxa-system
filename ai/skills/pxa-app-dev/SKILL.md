---
name: pxa-app-dev
description: Create or modify PXA C++26 applications using the standalone DevKit, including declarative UI, game loops, resources and service declarations. Use for PXA app implementation, not unrelated C++ or firmware builds.
---

# PXA application development

When Agent Kit setup is present, use workspace `.pxa/pxa` (Windows `.pxa/pxa.cmd`) instead of bare `pxa`; MCP tools expose the same implementation. Apps belong in `apps/<name>`. Start with `doctor`; do not rebuild the firmware environment for an app task. Follow the user goal through actual runtime and visual checks, then iterate on feedback.

Read the existing project and `pxa.lock` before choosing the SDK. Use the user's selected DevKit; `pxa --version` and `pxa run --help` reveal its actual capabilities. PXA 0.5.0 requires Core 2.1; SDK release, Core ABI, service versions and CPU/AOT compatibility are separate concepts.

For a new app, `pxa init <directory> --id <unique-id>` generates the working C++26 template. Preserve existing source, identity, signing key and release sequence when editing an app. Do not recreate a populated directory or implicitly update its SDK lock. Work outside the firmware checkout when only building an app.

Use `pxa build <project> --offline --target simulator` and `pxa run <project> --offline --debug` for the development loop. Omit `--offline` if downloading the pinned toolchain is needed and authorized. Build first, then verify real Guest readiness, take a screenshot, inject the relevant input and inspect the resulting screen; a successful build is not a working UI. Read the `PXADB: ... --simulator PROFILE@INSTANCE` line emitted by this run and use that exact selector. Stop the CLI you started when finished; it owns its simulator and debug bridge.

For SDK code, read [C++ implementation notes](references/cpp.md) only for the relevant module. Prefer the installed SDK's headers and examples to guessed APIs or historical ABI directory names. A firmware workspace's `tools/app.sh` and `tools/dev.sh` are a separate workflow; they are not shipped in the standalone DevKit.

If `$pxa-debug` is installed, use its verification helper for screen profiles and scripted interaction. Otherwise use the CLI directly. Keep app service declarations aligned with the APIs actually used, handle `Result` failures, and use optional modules rather than default caches. For games, measure displayed frames, input response and current/peak memory with a reproducible workload before claiming a performance benefit.

Finish with the implemented behavior, commands executed, screenshot evidence and any unverified device requirements. Continue routine edits/builds/testing within the user's scope; installation on hardware, publishing and firmware updates use the authorization already provided by the user.
