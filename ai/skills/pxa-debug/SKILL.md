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

The default covers the five common profiles; `--profiles` selects the requested target matrix. It builds once offline, then starts each real Guest with an owned debug bridge, checks active app identity, captures a screen, and stops its processes. Once execution starts, it writes `result.json` even on failure; exit 0 means runtime checks passed, not visual correctness. Review screenshots before accepting layout or interaction. Evidence directories must be new and outside the app; app build caches/lock and daily simulator saves are preserved; each verification profile gets its own fresh test state. `--headless` uses SDL dummy; omit it for an actual desktop window. Use `--online` only when a pinned dependency needs downloading.

Use `--jobs 1..4` (default 2) for bounded profile workers. Each gets separate
state, publisher output and debug sockets; a failure keeps its diagnostics while
the other requested profiles finish. All workers validate and run the same package
hash. The result includes requested geometry, actual Host metrics when available,
step times, build receipt and original capture dimensions/hashes. `contact-sheet.png`
is a labelled review preview of the last captured state; it is excluded from the
original screenshot manifest and must never be uploaded as a store screenshot.

Use `--no-build` only for a prepared local build. Validation checks source/common
inventory, compiler/CMake dependencies and declared generator inputs, resources, SDK/toolchain lock, target,
exact `--define` values, signing identity and package hash. Stale/missing inputs or
old receipts fail before simulator launch; they never trigger an implicit rebuild
or lock update. Prepare once with `verify` or SDK `build`, then reuse for another
profile/scenario. SDK `build --check-existing` validates only; SDK `run --no-build`
also supports explicit `--simulator-state` for isolated manual checks. The prepared
build's Debug/Release mode is reported; enabling the debug bridge does not recompile
it. Repeat accelerated test definitions, and build the final package without them.

When the locked SDK lacks checked reuse, normal `verify` builds once and runs
those frozen package bytes in fresh profile states through the legacy native
launcher. It preserves the lock, signing identity and daily saves; it does not
claim a dependency inventory for the old receipt. `--no-build` fails before
launch in this case. Missing UI inspection on an old Host still fails semantic
assertions; use supported input/screenshots and review them, or explicitly migrate
the workspace and then the app with `sdk update`. Never patch an installed SDK.

New C++ apps include `tests/basic.json`: it checks 0, taps the +1 button and waits
for 1 on each selected profile. Replace those assertions with the requested app's
actual behavior as you replace the template UI.

For teaching/help changes, exercise first use, dismissal/completion, sustained
normal use, `relaunch` with the same isolated test save and reopening Help.
Wait for the app's persistence completion before relaunching. Check the ordinary
post-tutorial screen for unnecessary copy and HUD obstruction, alongside clear
labels, recovery feedback and accessibility. Use pxa-app-dev's interface-content
reference when available. Absence assertions only cover real retained text;
Surface/Canvas content requires actual interaction and image review.

For interaction, read [scenario format](references/scenarios.md), supply `--scenario`, then compare before/after images. Pixel coordinates are per profile, not dp. The helper does not guess control locations, OCR, infer game FPS or silently clear saves. A missing memory capability is recorded as unavailable, never as zero usage.

Use `pxa_ui_inspect` (CLI `pxa device ui inspect --simulator PROFILE@INSTANCE`)
to read the actual Guest node IDs, parents, text, values, visibility, enabled state
and resolved pixel rectangles. MCP returns a bounded page (64 nodes by default)
and saves the complete snapshot; `limit` / `offset` select fresh observations,
whose hashes may change while the app runs. Use `pxa_tap_node(selector=...)` for a uniquely
identified control, then `pxa_ui_expect(selector=..., expected=..., timeout_ms=...)`
to verify the resulting state. Exact labels and typed controls are preferable to
guessed coordinates for ordinary UI. Passwords are redacted; hidden/disabled and
ambiguous matches cannot be clicked. Node IDs belong to the current tree and may
change after rebuilding a subtree. Bounds/clip rectangles do not prove a node is
unobscured by overlays or physical masks; inspect screenshots and input effects.
On hardware, also confirm the display is awake, the lock screen is dismissed
and the intended app is in front. A hidden Guest tree can remain inspectable
while native lock screens or permission dialogs intercept its input. Use the
visible system controls and verify the resulting screenshot before retrying.
Older Hosts/DevKits lacking this debugger fail explicitly; select a matching
release through the normal explicit SDK workflow, never patch its installed code.

Check visible text and controls whose `clip` is smaller than their `bounds`;
successful text assertions and taps can still leave a title or button partly cut
off. Inspect the original image and correct the layout within the actual safe
area. Run round and rounded profiles with their ordinary display settings for
this check. Store capture settings disable those masks and insets, so a passing
rectangular Store capture does not establish that the ordinary device layout fits.
Check the recorded steps for each requested profile: an empty step list proves
launch only. Add the intended journey for that profile before claiming its controls
or persistence were verified; SDK templates need not cover every supported profile.

`--require-ui` now checks visible retained content, including Canvas, without a
central-pixel heuristic. It detects a missing/hidden tree but does not prove that
a Canvas drew the right pixels. Surface-only apps should omit this flag and check
actual frames and interactions. Add `expect` / `wait-for` scenario steps to assert
specific behavior; a successful input acknowledgement alone is insufficient.

For live MCP debugging, prefer `pxa_interact(job_id, steps)` for a short, known sequence on an owned `pxa_run` job. It reuses one SDK debug session. Poll the returned batch job, check its scope and runtime result, then inspect every relevant image with `pxa_view_image`. Evidence paths are planned until the job finishes. Read the [batch format and limits](references/scenarios.md). Do not send concurrent input to that simulator or batch steps that require seeing an intermediate result first. Use CLI `verify --scenario` when MCP is unavailable.

For manual live debugging, run `pxa run <project> --debug --watch` and use the exact emitted selector with `pxa device info`, `input`, `screenshot`, `package list` or `memory`. A failed watch rebuild should retain the previous running Guest. Inspect build diagnostics for compiler errors, actual INFO/Core/CPU/AOT capabilities for incompatibility, and package/public-key identity for verification errors. Do not conceal errors by restarting forever or changing ABI/budgets without evidence.

Use [measurement notes](references/measurements.md) when profiling games or device memory. After a fix, repeat the failing action and relevant screen profiles; broader checks are needed only if a new failure or cross-cutting change warrants them. Report what was actually displayed and changed, not just command exit codes. Hardware validation belongs to `$pxa-device-test` if installed.

## Store screenshots

Use `.pxa/pxa verify apps/<name> --profiles pai-touch --store-screenshots`
(or MCP `pxa_verify(store_screenshots=true)`) to capture the real PXA Guest
in a rectangular viewport with rounded/circular masks, safe insets and system
navigation disabled at startup. Disable any performance overlay before capture,
including app-owned debug UI. Inspect every original PNG before upload and
check the complete rectangular frame for performance overlays, bottom gesture
bars or three-button navigation. Use the captured originals directly.
Never crop, retouch or generate replacement screenshots.
The result records capture settings and original SHA-256 hashes;
runtime success does not imply visual approval.

For apps with a signed resource binding, pass
`--resource-package local/artifacts/game/resources.pxa` to `verify` (MCP
`resource_package`). The verifier hashes that file, checks the exact App ID,
lineage and resource-manifest binding. Native installers verify signatures and
file digests in separate resource and code trees. Verified resource files are
exposed through the Host's read-only, app-scoped media root in each isolated
simulator; installing code cannot overwrite them.
The current isolated flow requires the same signing key for both containers.
Keep the resource binding intact; do not replace a resource-backed game with
an empty package just to obtain a passing launch check.

For environment, build, device or publishing failures, use the
[troubleshooting guide](https://pxa.doit.am/docs/ai-troubleshooting/).
