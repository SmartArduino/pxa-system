# PXA AI kit 0.1.0

[简体中文](README.md)

Optional skills, Codex agent definitions and repeatable verification for PXA C++26 apps. Tested with standalone DevKit 0.3.0, requiring Python 3.11+ (the DevKit's bundled Python also works). No firmware workspace, MCP service, API key or third-party Python module is required. Nothing runs on the device or adds Guest memory.

Download and extract [the release](https://github.com/SmartArduino/pxa-system/releases/tag/ai-kit-v0.1.0), then run from the project's parent directory:

```sh
python3 pxa-ai-kit-0.1.0/install.py list
python3 pxa-ai-kit-0.1.0/install.py install --project hello --agents --dry-run
python3 pxa-ai-kit-0.1.0/install.py install --project hello --agents
```

Create `hello` first with `pxa init hello` if needed. Skills go to `.agents/skills/`; optional agents go to `.codex/agents/`. The installer preserves AGENTS.md, config.toml, global settings, model and permission choices. `--skill pxa-debug` selects one skill. Reinstalling identical files is a no-op; `--update` only replaces unmodified kit-owned files. `remove --project hello` removes only managed files and refuses local edits. Move edits you want to keep before removing. Do not run concurrent installers.

Use `$pxa-app-dev` for implementation, `$pxa-debug` for simulator reproduction, `$pxa-device-test` for authorized hardware testing, and `$pxa-release` for packaging/publishing preparation. Request `pxa_developer` and `pxa_tester` explicitly for independent implementation/verification. Clients without custom agents can still use the skills in one agent. Each TOML's developer_instructions also works as a manually loaded role brief. The root plugin.json follows the portable plugin layout; public directory listing is not implied.

```sh
python3 hello/.agents/skills/pxa-debug/scripts/verify_app.py hello \
  --pxa /path/to/devkit/bin/pxa --output hello-evidence --headless
```

The helper runs real Guests on five profiles, checks active identity, captures screenshots, and supports a bounded [JSON input scenario](skills/pxa-debug/references/scenarios.md). Evidence must be new and outside the project. Normal build caches/lock and simulator saves persist; use a separate project for clean state. Add `--online` only if pinned dependencies need downloading. Runtime success is not visual correctness, display FPS or physical latency; review screenshots. Unsupported heap queries are recorded as unavailable. All owned processes are stopped.

The kit has a separate version and signed manifest; existing DevKit 0.3.0 assets/manifests remain immutable. Verify ai-kit-0.1.0.json.sig using a trusted DevKit's config/release-public.pem and compare the archive SHA-256 with the signed manifest before extraction. The internal file manifest detects corruption, not publisher authenticity. Skills respect existing user authorization and inherited permissions.

[Codex skills](https://developers.openai.com/codex/skills/) · [Custom agents](https://developers.openai.com/codex/subagents/) · [Portable plugins](https://developers.openai.com/plugins/build/plugins)
