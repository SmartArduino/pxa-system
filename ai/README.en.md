# PXA Agent Kit 0.7.2

0.7.2 adds `store build --target-timeout-seconds` (default 1800 per target, range 1–3600; same MCP parameter). The matrix job budget covers all targets plus mandatory Wasm. Timed-out or cancelled commands terminate their owned compiler process group, preserving unrelated jobs, pinned SDKs and optimization settings.

0.7.1 adds signed-resource game verification: `verify APP --resource-package local/artifacts/game/resources.pxa --store-screenshots` (MCP: `pxa_verify(resource_package=...)`). The verifier checks the exact resource binding, signing identity and file digests, then mounts verified assets in the isolated Host’s read-only media root. Use scenario input to enter a level and inspect original screenshots; launch success alone does not prove playability. Code and resources currently require the same signing key.

Pairs with DevKit and firmware 0.11.0; Browser Host has its own version. Networking guidance selects
actual Host capabilities, distinguishes native browser WebSockets from TCP,
listeners, UDP and DNS-SD, and covers permission lifetime and real peer tests.
Hardware verification accounts for native lock screens intercepting app input.
Screen rotation uses the existing Window 0.5 interface.

[中文](README.md) · [Tutorial](https://pxa.doit.am/docs/ai-development/)

Ask your coding assistant:

> Read https://pxa.doit.am/ai.md, set up PXA in this directory, and verify the environment by running the simulator. Then wait for my app requirements.

With [uv](https://docs.astral.sh/uv/getting-started/installation/) installed:

```sh
uv run --python 3.12 https://pxa.doit.am/setup.py --workspace ./my-pxa
```

The bootstrap verifies the release signature, archive hash and file manifest. Setup installs the signed DevKit 0.11.0, project-local skills/MCP adapters and launchers, then builds and runs an actual Guest. No ESP-IDF, LLVM build or additional model API key is needed. Native Linux x86_64 requires glibc 2.39+; Windows x64 can use the signed native DevKit (Wine-validated preview); macOS and older Linux use Docker Desktop or Podman with Linux amd64 support. OS administration, virtualization and user login may require normal user interaction.

Codex and Claude Code receive local skills, optional roles and MCP entries. DSH discovers shared `.agents/skills` and can use the CLI directly; `.pxa/dsh-mcp.yaml` is an optional official Cordis composition snippet. Other agents can use the standard stdio MCP server or CLI. Existing global settings, models, permissions, unrelated MCP servers and edited files are preserved. Reopen the project if the client has not discovered the new tools.

From the workspace (Windows: `.pxa\pxa.cmd`):

```sh
.pxa/pxa doctor
.pxa/pxa init apps/game --id pxa-my-game
.pxa/pxa run apps/game --profile pai-touch --watch
.pxa/pxa verify apps/game
.pxa/pxa install apps/game --port /dev/ttyACM0
```

DevKit0.10+ also supports `new apps/timer --recipe timer --title Timer --minutes 25`
and `--recipe counter`. Each starter includes touch/controller handling,
checkpoint storage and `tests/journey.json`; run it with `verify --scenario`.
Timer completion uses an isolated test key with `--define PXA_RECIPE_TIMER_SECONDS=6`
and `tests/complete.json`. Creation is not acceptance; inspect the real captures,
then build the delivered package without test macros. Existing apps/SDK locks stay intact.

`sdk cache`, `sdk prune --dry-run` and explicit `sdk repair --offline` manage signed
native SDK downloads. Setup/repair can retain archives with `--keep-archives`.
`clean apps/game --keep 2 --dry-run` previews old project caches; published outputs,
current referenced generations, keys and simulator saves are preserved. These
commands do not silently migrate an SDK. See the Chinese guide for full semantics.

Ask the AI to implement, build, run, exercise input and inspect screenshots, fixing observed failures until the goal is met. MCP long operations return jobs; poll status and inspect actual images. Logs/screenshots stay in ignored `local/artifacts/`. Keep app IDs, original signing identities, SDK locks and saves when iterating. CLI remains usable without MCP.

App/game UI defaults to task-relevant content and clear controls, without unsolicited slogans, decorative subtitles or repeated instructions. Teaching has a trigger, dismissal/completion, persisted progress and a help/replay entry. Preserve requested narrative, style, accessibility and necessary feedback. See [interface content](skills/pxa-app-dev/references/interface-content.md); a compact summary is returned by `init`, `new` and `context` and included in workspace/MCP guidance.

For games, run a complete playable loop early, select rendering/glyph/audio paths from the pinned SDK, reuse incremental builds, and measure cold text, busy interaction and frame pacing. See [game development](skills/pxa-app-dev/references/game-development.md). `verify --scenario` supports `relaunch` to reopen only the verified app with the same isolated test save. Wait for actual persistence before relaunch, then assert restored state and inspect normal post-tutorial use. Daily simulator saves are preserved.

Official firmware: `firmware prepare --board <board>` downloads and verifies without opening USB. An authorized `firmware upgrade --board <board> --port <port>` defaults to app-only and checks the real installed partition table before writing. A layout mismatch stops. Full images erase apps, saves and settings and require explicit user data-loss authorization plus `--mode full --erase-data`. S31 may require manual BOOT/RESET and a changed port. Flash completion and successful boot are distinct results.

Publishing: run `store login --wait` once and approve remembering this computer in the browser. `store publish <project> <package>` then reuses the account across apps, proves each local signing key and obtains/renews 30-minute app upload authority before submitting for review. Active use renews the computer credential; 365 days of inactivity requires login again. Supported installed Python keyring backends are preferred, with owner-private files outside the project as fallback. Secrets never enter tool output, chat or command arguments. Use `store account`, `store account --refresh`, `store logout`, or `store login --renew --wait` to inspect, renew, revoke or switch accounts. Revocation in the store's Publishing authorizations page immediately invalidates derived upload tokens. `store authorize` remains supported and `--app-only` retains legacy 30-day single-app approval. Low-level SDK `publish --token-file` remains app-scoped.

Run `store prepare <project>` first for current media specifications and taxonomy. Upload optional `--icon`, `--cover` and repeated `--screenshot` images. Prefer 512×512 icons and 1280×960 covers; offer image generation, supplied files or explicit `--skip-icon` / `--skip-cover`, preserving existing media when omitted. Existing listing fields remain unchanged unless explicitly supplied, including name/summary/description. For published apps, text/media edits create private drafts; approved content and published packages stay live through pending/rejected reviews. Approval promotes the draft. Inspect `listing_update`/`media_updates` for review status and `public_listing_retained`; do not withdraw or recreate the app to recover a pending listing. Submission preserves signature checks, quotas and store review.

Agent Kit 0.7.0 defaults to separate store packages per target: `store build APP --targets esp32s3 esp32s31 esp32p4`, followed by `store publish-targets APP MANIFEST` with the returned manifest path. Packages and `packages.json` stay under ignored workspace `local/artifacts/pxa-store-build/<run>/`, outside the app input inventory; builds reuse common caches. S31 and P4 remain separate targets. Select only requested native devices; the tool always adds a mandatory, separately signed portable Wasm `.pxa`, even for device-focused releases. Simulators are normally validation targets. Variants share identity, signing lineage, version and release sequence. Whole-set preflight requires Wasm and uploads it first; metadata/media upload once. Single native uploads first check that the same version/sequence/signer already has a separate Wasm package in the store. On partial failure inspect store state before retrying unfinished targets. Single-file `store publish` rejects multiple AOT targets unless `--allow-universal` was explicitly chosen. Generic multi-target builds still produce universal containers.

Write a factual one-sentence summary of the actual task or core gameplay, aiming for 20–60 Chinese characters or 8–25 English words (200-character limit). Apps describe the user action/result; games describe the main action and goal. Longer descriptions add only implemented features and material limitations. Omit slogans, generic praise, AI/PXA credits and control tutorials. Do not invent offline use, saves, multiplayer, compatibility or performance. Save copy in `store.json` and resolve `store prepare` → `listing_text.needs_text`; inspect the app before filling gaps. No generic placeholder summary or silent truncation is provided.

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
`pxa_verify(store_screenshots=true)`. This uses the DevKit's
`run --store-screenshots` with a rectangular simulator viewport, zero corner
radius and safe insets, hidden navigation and simulator profiling disabled.
Disable app-owned performance overlays before capture. Inspect every original
image for a complete rectangular frame and hidden overlays/navigation before
upload. Never crop, retouch or generate replacement screenshots. Evidence records
capture settings and original image hashes. Visual review remains required.

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

Context follows the project SDK lock, fails if that release is missing, and searches bundled C++ extension headers/examples. App scope also protects extension SDK sources. Stop watch before verify; run already builds incrementally. Only read-only GET/HEAD requests retry transient network failures; uploads and authorization writes are never replayed.

For delivery, `pxa_build` accepts `targets=["esp32s3", "esp32s31", "esp32p4", "simulator", "windows-simulator", "wasm"]` in one shared build. Keep the default simulator target during iteration. Poll jobs with `include_log=false` unless diagnostics are needed; completed elapsed times remain stable.

`device-profile` / MCP `pxa_device_profile` reads actual CPU, resolution, debug inputs and sensor descriptors, with actionable USB/PXADB setup guidance. Confirm physical controls and adaptive screen requirements. Pixel-game guidance covers Z Labs Chinese fonts, integer nearest-neighbor scaling and input mapping. `store prepare` returns a publishing checklist; generate matching icons/artwork when available, skip optional covers without image generation, and capture original rectangular simulator screenshots. Native Windows x64 signed component installation is supported alongside Linux and the container route.
