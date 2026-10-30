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

## Listing artwork, taxonomy and extensions

Run `store prepare <project>` (MCP `pxa_store_prepare`) before authorization or
submission. It reads the current store publishing specification and reports
available categories, tag limits, image formats/limits and `needs_choice`.
Prefer a 512×512 icon and a 1280×960 (4:3) illustrated cover. When an image tool
is available, offer to generate artwork that matches the application. Otherwise
ask the user to provide images or choose to skip either upload. Do not block
publishing on access to an image-generation service. Record explicit choices
using `--skip-icon` / `--skip-cover`, or `"icon": null` / `"cover": null` in
`store.json`; skipping preserves existing store artwork and does not delete it.
Artwork paths in store.json are relative to the project; CLI paths are relative
to the workspace. Do not claim a generated cover is a real runtime screenshot.

Set `kind` (`app` or `game`), its category and descriptive tags in `store.json`.
Use the server's category list, at most ten labels of 24 characters each.
`store publish` uploads `--icon`, `--cover` and `--screenshot`, and applies
explicit taxonomy/tag fields to existing listings using the app-scoped token.
An empty tags list clears tags; absent fields retain the existing values.
Changes follow the existing listing review process. Preflight all images before
any upload and do not interpret pending review as publicly listed.

Use standard SDK services when they implement the feature. Otherwise reuse the
workspace's pxa-ext extension and its matching SDK, or define an identified,
version-negotiated extension with bounded messages, permissions and teardown.
Document nonstandard contracts as extensions; do not invent standard service
IDs. If a future standard covers it, document migration to that standard.

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
