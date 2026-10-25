# PXA Agent Kit 0.3.3

[中文](README.md) · [Tutorial](https://pxa.doit.am/docs/ai-development/)

Ask your coding assistant:

> Read https://pxa.doit.am/ai.md, set up PXA in this directory, and verify the environment by running the simulator. Then wait for my app requirements.

With [uv](https://docs.astral.sh/uv/getting-started/installation/) installed:

```sh
uv run --python 3.12 https://pxa.doit.am/setup.py --workspace ./my-pxa
```

The bootstrap verifies the release signature, archive hash and file manifest. Setup installs the signed DevKit 0.5.0, project-local skills/MCP adapters and launchers, then builds and runs an actual Guest. No ESP-IDF, LLVM build or additional model API key is needed. Native Linux x86_64 requires glibc 2.39+; macOS, Windows and older Linux use Docker Desktop or Podman with Linux amd64 support. OS administration, virtualization and user login may require normal user interaction.

Codex and Claude Code receive local skills, optional roles and MCP entries. DSH discovers shared `.agents/skills` and can use the CLI directly; `.pxa/dsh-mcp.yaml` is an optional official Cordis composition snippet. Other agents can use the standard stdio MCP server or CLI. Existing global settings, models, permissions, unrelated MCP servers and edited files are preserved. Reopen the project if the client has not discovered the new tools.

From the workspace (Windows: `.pxa\pxa.cmd`):

```sh
.pxa/pxa doctor
.pxa/pxa init apps/game --id pxa-my-game
.pxa/pxa build apps/game
.pxa/pxa run apps/game --profile pai-touch
.pxa/pxa verify apps/game
.pxa/pxa install apps/game --port /dev/ttyACM0
```

Ask the AI to implement, build, run, exercise input and inspect screenshots, fixing observed failures until the goal is met. MCP long operations return jobs; poll status and inspect actual images. Logs/screenshots stay in ignored `local/artifacts/`. Keep app IDs, original signing identities, SDK locks and saves when iterating. CLI remains usable without MCP.

Official firmware: `firmware prepare --board <board>` downloads and verifies without opening USB. An authorized `firmware upgrade --board <board> --port <port>` defaults to app-only and checks the real installed partition table before writing. A layout mismatch stops. Full images erase apps, saves and settings and require explicit user data-loss authorization plus `--mode full --erase-data`. S31 may require manual BOOT/RESET and a changed port. Flash completion and successful boot are distinct results.

Publishing: `store authorize <project> <package> --wait` opens browser login and one-app approval. Local P-256 signing proves key possession; private keys remain local. An app-scoped 30-day upload credential is stored privately outside the project, never printed in logs or command arguments. Existing listings are reused without overwriting metadata. `store publish <project> <package>` submits the exact signed bytes for review, optionally with `--icon` and `--screenshot`. It does not bypass store review. See the Chinese README for offline installation, integration locations and limitations.

## Scoped workflows in 0.3.3

Create real C projects with `init apps/demo --id pxa-demo --language c`; C++ remains the default.
Use `context apps/demo --query ui --language c` for bounded declarations and examples from the pinned SDK. Keep incremental build caches; verify one affected screen during iteration and the required profile matrix before delivery. Scenarios now support controller holds and automatic input cancellation.

Begin `scope begin --mode app --project apps/name` before editing an existing app; init begins it for new apps. Only that app may change. SDK, ABI, runtime, simulator and board source are read-only. Scope checks preserve prior dirty state and detect later changes; they are audits, not OS sandboxes. Git trees include tracked/non-ignored files; standalone SDK checks cover SDK/interface/configuration/developer-tool source, not distribution toolchain binaries. Inspect the job's scope/passed fields, not only its subprocess exit code. `scope end` archives only a passing scope; repeated setup checks and archives it before updating managed tools.

For board work, read https://pxa.doit.am/board-agent.md and use a separate firmware checkout:
`uv run --python 3.12 https://pxa.doit.am/setup.py --workspace . --mode board`.
This installs skills/MCP without installing an app DevKit or ESP-IDF. Use that checkout's documented firmware toolchain.
`board init my-board --target esp32s3 --reference pai-touch` generates board-local callbacks and an explicit bring-up gate. Complete schematic-backed hardware facts and real drivers/partitions before `board check` and `board build` can pass. The pxa-board-port skill and optional pxa-board-developer role restrict writes to the selected BSP/profile/generated dependency lock. Keep SDK, PXA interfaces and shared code unchanged; new CPU backends are separate platform work. Static readiness is not physical acceptance.

## Context and store captures

Context lookup prioritizes exact symbols and declarations, combines nearby excerpts,
and lists headers for an empty query. Verification uses the pinned SDK's PXADB
directly when available. Headless live jobs pass SDL settings into the container.

Use `verify apps/name --profiles pai-touch --store-screenshots` or MCP
`pxa_verify(store_screenshots=true)`. Navigation is hidden before capture.
Disable performance overlays, inspect every original image, and never crop or
retouch system controls away. Evidence records capture settings and image hashes;
runtime success is not visual approval.

For related live simulator actions, use MCP `pxa_interact(job_id, steps)`
to reuse one SDK session: tap, swipe, controller, wait, screenshot or cancel.
A batch permits 1–32 ordered steps, 8 screenshots and 15 seconds of holds/waits.
Poll its returned job before reading evidence, then inspect original images.
Do not overlap inputs on the same simulator; split a batch whenever the next
action depends on a visual result. CLI users can use `verify --scenario`.

Completed batch jobs include their `batch` result in `pxa_job_status`.
Use `pxa_job_stop` to cancel the actual worker and release input, including
inside containers. Normal cancellation keeps the simulator available; an
unresponsive worker forces only its owned simulator to stop. Cancellation
never passes verification. Inspect current state before continuing instead
of replaying steps that may already have taken effect.
