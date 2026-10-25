---
name: pxa-debug
description: Reproduce PXA app failures with the simulator, screenshots and scripted touch/controller input; verify UI across screen profiles within the app-only change boundary.
---

# PXA debugging and verification

When Agent Kit setup is present, use workspace `.pxa/pxa` (Windows `.pxa/pxa.cmd`) instead of bare `pxa`; MCP tools expose the same implementation. Apps belong in `apps/<name>`. Start with `doctor`; do not rebuild the firmware environment for an app task. Follow the user goal through actual runtime and visual checks, then iterate on feedback. Use `.pxa/pxa verify <app>` or `pxa_verify`, poll its job, and inspect resulting images. Keep evidence in ignored `local/artifacts/`.

Identify the app, pinned DevKit, failing action and expected screen. Reproduce before editing. Use a separate checkout or test project when editing a live app would interfere with another developer. Keep the user's existing lock, signing identity and data.

Use the app's active change scope. SDK, ABI, runtime and board sources are
read-only during app debugging; a missing platform capability is a separate
task. Run scope check before delivery. Poll long MCP jobs with a bounded wait
and omit repeated logs; request the log when the job finishes or fails.

For repeatable simulator checks, run the bundled [verification helper](scripts/verify_app.py):

```sh
python3 <skill-directory>/scripts/verify_app.py <project> \
  --pxa /path/to/devkit/bin/pxa --output /path/to/new-evidence --headless
```

The default covers all five DevKit 0.5.0 profiles. It builds offline, starts each real Guest with an owned debug bridge, checks active app identity, captures a screen, and stops its processes. Once execution starts, it writes `result.json` even on failure; exit 0 means runtime checks passed, not visual correctness. Review screenshots before accepting layout or interaction. Evidence directories must be new and outside the app; app build caches/lock and simulator saves persist normally. `--headless` uses SDL dummy; omit it for an actual desktop window. Use `--online` only when a pinned dependency needs downloading.

For interaction, read [scenario format](references/scenarios.md), supply `--scenario`, then compare before/after images. Pixel coordinates are per profile, not dp. The helper does not guess control locations, OCR, infer game FPS or silently clear saves. A missing memory capability is recorded as unavailable, never as zero usage.

For manual live debugging, run `pxa run <project> --debug --watch` and use the exact emitted selector with `pxa device info`, `input`, `screenshot`, `package list` or `memory`. A failed watch rebuild should retain the previous running Guest. Inspect build diagnostics for compiler errors, actual INFO/Core/CPU/AOT capabilities for incompatibility, and package/public-key identity for verification errors. Do not conceal errors by restarting forever or changing ABI/budgets without evidence.

Use [measurement notes](references/measurements.md) when profiling games or device memory. After a fix, repeat the failing action and relevant screen profiles; broader checks are needed only if a new failure or cross-cutting change warrants them. Report what was actually displayed and changed, not just command exit codes. Hardware validation belongs to `$pxa-device-test` if installed.

## Store screenshots

Use `.pxa/pxa verify apps/<name> --profiles pai-touch --store-screenshots`
(or MCP `pxa_verify(store_screenshots=true)`) to capture the real PXA Guest
with system navigation hidden at startup. Disable any performance overlay
before capture, including app-owned debug UI. Inspect every original PNG
before upload: no performance overlay, bottom gesture bar or three-button
navigation. Never crop or edit an image to conceal these elements.
The result records capture settings and original SHA-256 hashes;
runtime success does not imply visual approval.

For environment, build, device or publishing failures, use the
[troubleshooting guide](https://pxa.doit.am/docs/ai-troubleshooting/).
