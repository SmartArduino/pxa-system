# PXA Agent Kit 0.2.1

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
