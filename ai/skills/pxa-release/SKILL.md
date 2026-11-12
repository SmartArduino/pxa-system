---
name: pxa-release
description: Prepare and verify signed PXA app packages, publisher proofs and app-store uploads, or review a PXA DevKit release; preserve immutable artifacts, scoped credentials and Chinese-first bilingual release notes.
---

# PXA packaging and release

When Agent Kit setup is present, use workspace `.pxa/pxa` (Windows `.pxa/pxa.cmd`) instead of bare `pxa`; MCP tools expose the same implementation. Apps belong in `apps/<name>`. Start with `doctor`; do not rebuild the firmware environment for an app task. Follow the user goal through actual runtime and visual checks, then iterate on feedback. Check `store account`; first use needs `store login --wait` in the browser, then `store publish` reuses that computer's account and automatically obtains short-lived app upload authority. Never request copied tokens or private keys in chat. Submission remains pending review.

Determine whether the request concerns an app, the DevKit or this optional AI kit. App version/sequence, SDK release, Core ABI and AI-kit version are distinct. Preserve the app's signing identity and lineage; increment its release sequence for an update. Never overwrite an existing release asset or move a published tag to repair a release.

For apps, use `store build APP --targets ...` / `pxa_store_build` for the requested delivery targets. It uses the locked SDK's caches and creates an independently signed package per target. Validate the corresponding simulator/device behavior; a valid signature does not prove runtime compatibility. Keep signed original bytes and provenance. Do not hand-edit the final container.

Read the current [store tutorial](https://pxa.doit.am/docs/store-publishing/) when publisher registration or upload is needed. `challenge` is a one-time verification string (一次性验证串). The public publisher-proof script uses P-256, SHA-256 and canonical low-S P1363. Use the same private key that signed the package; keep the key local. Upload tokens are app-scoped, expire and may be revoked; use `--token-file`, not command arguments or a copied secret in instructions.

The remembered computer credential can authorize the account's own apps after
each app's local key proof; it cannot log into ordinary account/admin APIs.
Active use renews its lease and expired app tokens are replaced before upload.
Do not open browser approval again for each new app or release. `store logout`
revokes this computer and clears local app credentials; `store login --renew`
switches accounts. A revoked/expired account or unavailable credential store
needs recovery/login, not a write retry loop. `store authorize --app-only`
retains legacy single-app approval when requested or when using an older server.
Native keyring storage is used when a supported Python keyring backend is
available; otherwise credentials use owner-private files outside the project.
Stored account authority does not itself request publication of an app: follow
the user's actual submission instruction and existing authorization.

Prepare the concrete package, checks and Chinese-first / English-second release description before the final publication step. Publishing, pushing tags and production deployment require the user's applicable authorization; an existing instruction to publish authorizes the matching step and is not a reason to ask again. A generic instruction to develop an app does not authorize production publishing. Do not turn a rejected upload into an unbounded retry or weaken server quotas/signatures.

For DevKit/AI-kit maintainers, inspect `tools/release/assemble.py`, `build_ai_kit.py` and the current release manifest rather than assuming new CLI commands exist in an older binary. Use committed source, fixed tool versions, reproducible archives and a signed manifest; never package private keys, upload tokens, app saves or machine-specific paths. After release, download the actual assets, verify hashes/signatures and execute the documented install/use flow in a clean project. Describe hardware and platform limits explicitly.

## Listing artwork, taxonomy and extensions

Default to one architecture/target per uploaded `.pxa`, including separate S31
and P4 packages despite both using RISC-V. Keep the app ID, signing lineage,
version and release sequence identical across variants. Always include a separate
Wasm `.pxa` containing portable `.wasm` code, even for device-focused releases;
`store build` adds it automatically. Build only requested native device targets.
Simulator targets are normally validation builds, not store deliverables.
`store build` saves packages and its `packages.json` in ignored workspace
`local/artifacts/pxa-store-build/<run>/`, outside the app's source inventory.
Pass the returned manifest to `store publish-targets APP MANIFEST` /
`pxa_store_publish_targets`. It validates hashes, signed targets, release
identity and checked build inputs before any upload, then submits variants of
one release, uploads Wasm first and uploads listing/media once. A missing Wasm
variant blocks submission. Single native uploads also check the store for a separate
Wasm package with matching version/sequence/signer; use this to complete partial
submissions without replaying successful uploads. Old/stale inputs require an explicit
rebuild. On partial failure inspect store state and retry only unfinished targets;
do not replay the full batch. Generic `pxa_build(targets=...)` still creates one
universal container. Single-package store uploads reject multiple AOT targets by
default; `--allow-universal` is for an explicitly requested universal package.

Run `store prepare <project>` (MCP `pxa_store_prepare`) before authorization or
submission. It reads the current store publishing specification and reports
available categories, tag limits, image formats/limits and `needs_choice`.
Prefer a 512×512 icon and a 1280×960 (4:3) illustrated cover. When an image tool
is available and listing artwork is requested, generate artwork that matches the
application. Without image generation, create a simple app-level vector icon or
use user-provided artwork; skip the optional cover and explain that choice. Do not block
publishing on access to an image-generation service. Record explicit choices
using `--skip-icon` / `--skip-cover`, or `"icon": null` / `"cover": null` in
`store.json`; skipping preserves existing store artwork and does not delete it.
Artwork paths in store.json are relative to the project; CLI paths are relative
to the workspace. Do not claim a generated cover is a real runtime screenshot.

Set `kind` (`app` or `game`), its category and descriptive tags in `store.json`.
For generated names, summaries and descriptions, read [store listing copy](references/store-listing.md).
Use one factual sentence for the summary, describing the real task or core gameplay.
`store prepare` returns listing guidance and text issues. Resolve these by inspecting
the product before authorizing; do not use generic placeholder copy or invent features.
Use the server's category list, at most ten labels of 24 characters each.
`store publish` uploads `--icon`, `--cover` and `--screenshot`, and applies
explicit name/summary/description/taxonomy/tag fields to existing listings using the app-scoped token.
An empty tags list clears tags; absent fields retain the existing values.
For published apps, edits create a private listing/media draft while the approved
listing, images and published packages remain visible. Approval promotes the
draft; rejection keeps the approved content live. Inspect `listing_update` for
review status and `public_listing_retained`; do not withdraw, delete/recreate an
app, bump its package sequence or repeatedly upload to "restore" a pending listing.
Admin/developer suspension still controls visibility independently.
Preflight all images before
any upload and do not interpret pending review as publicly listed.

Use standard SDK services when they implement the feature. Otherwise reuse the
workspace's pxa-ext extension and its matching SDK, or define an identified,
version-negotiated extension with bounded messages, permissions and teardown.
Document nonstandard contracts as extensions; do not invent standard service
IDs. If a future standard covers it, document migration to that standard.

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

For environment, build, device or publishing failures, use the
[troubleshooting guide](https://pxa.doit.am/docs/ai-troubleshooting/).

For a one-sentence publishing request, infer the listing draft from the app and
ask only for missing public name/description, intended audience/category, asset
rights and first-use publisher-account approval. Reuse an existing account. Report a concrete checklist from
`store prepare`: signed target packages, icon, original simulator screenshots,
optional generated cover, changelog and first-use browser authorization. Do not ask users
to supply screenshots the tools can capture or to paste private credentials.
