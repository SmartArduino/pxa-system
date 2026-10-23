---
name: pxa-release
description: Prepare and verify signed PXA app packages, publisher proofs and app-store uploads, or review a PXA DevKit release; preserve immutable artifacts, scoped credentials and Chinese-first bilingual release notes.
---

# PXA packaging and release

When Agent Kit setup is present, use workspace `.pxa/pxa` (Windows `.pxa/pxa.cmd`) instead of bare `pxa`; MCP tools expose the same implementation. Apps belong in `apps/<name>`. Start with `doctor`; do not rebuild the firmware environment for an app task. Follow the user goal through actual runtime and visual checks, then iterate on feedback. Prefer `store authorize <project> <package>` for browser short-code approval, then `store publish`; app-scoped credentials stay private outside the project. Never request copied tokens or private keys in chat. Submission remains pending review.

Determine whether the request concerns an app, the DevKit or this optional AI kit. App version/sequence, SDK release, Core ABI and AI-kit version are distinct. Preserve the app's signing identity and lineage; increment its release sequence for an update. Never overwrite an existing release asset or move a published tag to repair a release.

For apps, build the requested targets using the locked DevKit, check each package with `pxa publish <package> --check-only`, then validate on the corresponding simulator/device. A valid signature does not prove every component has a matching AOT artifact, services or budget on a device. Keep signed original bytes and provenance. Do not hand-edit the final container.

Read the current [store tutorial](https://pxa.doit.am/docs/store-publishing/) when publisher registration or upload is needed. `challenge` is a one-time verification string (一次性验证串). The public publisher-proof script uses P-256, SHA-256 and canonical low-S P1363. Use the same private key that signed the package; keep the key local. Upload tokens are app-scoped, expire and may be revoked; use `--token-file`, not command arguments or a copied secret in instructions.

Prepare the concrete package, checks and Chinese-first / English-second release description before the final publication step. Publishing, pushing tags and production deployment require the user's applicable authorization; an existing instruction to publish authorizes the matching step and is not a reason to ask again. A generic instruction to develop an app does not authorize production publishing. Do not turn a rejected upload into an unbounded retry or weaken server quotas/signatures.

For DevKit/AI-kit maintainers, inspect `tools/release/assemble.py`, `build_ai_kit.py` and the current release manifest rather than assuming new CLI commands exist in an older binary. Use committed source, fixed tool versions, reproducible archives and a signed manifest; never package private keys, upload tokens, app saves or machine-specific paths. After release, download the actual assets, verify hashes/signatures and execute the documented install/use flow in a clean project. Describe hardware and platform limits explicitly.
