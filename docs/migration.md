# Migration Plan

Progress on the current branch is tracked in
[Implementation status](implementation-status.md). Milestones are exit
conditions, not claims that adding an interface alone completes the migration.

## 1. Current boundaries

The current `app_pages` component combines several responsibilities:

- application/page registration and navigation;
- LVGL shell widgets and global overlays;
- application models and business callbacks;
- device status and input translation;
- PXA package catalog, launch and permission UI integration.

`page_manager` combines an LVGL page stack with a cross-thread event queue.
The PXA ESP component currently depends on `app_pages` to bind catalog and
prompt callbacks. This dependency must eventually be inverted.

Migration is incremental. Existing product behavior remains available while
contracts and replacements gain host-side tests.

## 2. Milestone 1: portable foundation

- Publish architecture and protocol drafts.
- Add a standalone portable core component with identity validation, an
  application registry and runtime-provider descriptors.
- Use caller-provided allocation and no ESP/LVGL headers.
- Add host tests for identity collisions, provider selection and ownership.

Exit condition: native and PXA metadata can normalize into one registry without
using `app_page_id_t`.

## 3. Milestone 2: lifecycle and runtime providers

- Implement the application instance state machine and task manager.
- Add a native-static runtime provider.
- Add a PXA runtime provider that wraps the existing libpxa host.
- Define manifest normalization for compiled native descriptors.

Exit condition: a native test application and a PXA test application produce
the same observable lifecycle trace.

## 4. Milestone 3: Intent and system message broker

- Add the contract IDL toolchain and native/PXA bindings.
- Implement explicit and filtered Intent resolution.
- Implement cross-application RPC, results and bounded topic events.
- Bridge libpxa IPC without exposing runtime-specific endpoint namespaces.

Exit condition: native-to-PXA and PXA-to-native launch, result and RPC tests
pass using the same contract vectors.

## 5. Milestone 4: compositor and renderer separation

- Define logical window/surface and renderer SPIs.
- Adapt `pxa_lvgl_ui` as the first portable renderer.
- Extract LVGL navigation mechanics from `page_manager` into an LVGL compositor
  backend; move event routing to the portable core.
- Add a headless reference renderer and environment tests.

Exit condition: the same portable test UI runs against LVGL and headless
backends, and no core public header exposes LVGL.

## 6. Milestone 5: reference shell and themes

- Implement role resolver and product profile.
- Extract home, status bar, navigation, lock screen and system dialogs into
  reference role packages.
- Add Theme service, semantic tokens and atomic light/dark changes.
- Preserve an immutable recovery shell.

Exit condition: a product can replace home, settings or status bar with either
a native or authorized PXA provider without changing core code.

## 7. Milestone 6: migrate current applications

Migrate one application at a time from `app_pages`:

1. application catalog and manager;
2. settings and device information;
3. alarm and Bluetooth audio;
4. AI chat and camera;
5. textbook and call;
6. file and package management.

Each application first adopts identity, lifecycle, Intent and service bindings.
Its direct LVGL UI may temporarily remain backend-specific. Portable UI
migration can then occur independently.

Exit condition: `app_pages.c` no longer owns application identity, business
callbacks, navigation policy or PXA bindings. Remaining LVGL code is confined
to applications or renderer implementations.

## 8. Dependency target

```text
application -> generated contract binding -> portable system core
portable system core -> provider SPIs
native runtime -> portable system core
PXA runtime -> portable system core + libpxa
LVGL renderer -> renderer SPI + LVGL
ESP platform -> provider SPIs + ESP-IDF
```

Neither `libpxa` nor a runtime provider depends on a concrete desktop, settings
page, status bar or product board implementation.

## 9. Package identity persistence migration

The current ESP package store uses App ID as its directory, policy and private
data key. The portable registry already uses `(publisher root, App ID)`, but
changing only the catalog record would allow two publishers to collide in
storage. Persistence migration is therefore a versioned store transaction:

1. Encode a filesystem-safe publisher-root digest and use the flat
   `<publisher-root-hex>~<app-id>` key beneath package, owner and private-data
   roots. External APIs use `<publisher-root-hex>:<app-id>`.
2. Add store APIs that accept the complete verified identity. App-ID-only APIs
   remain compatibility wrappers only while product-wide App ID uniqueness is
   enforced.
3. On boot, verify each legacy package manifest, derive its publisher root,
   install it through the recoverable three-slot transaction, then migrate
   owner-proven private data, declared permission grants and disabled policy.
   Each phase is repeatable after power loss; data without a matching owner
   record remains quarantined under its legacy key.
4. Detect two legacy records mapping to one canonical identity as corruption;
   never select one by directory enumeration order.
5. Keep the verified legacy source until the composite install commits. The
   package transaction supplies directory barriers; policy stores retain their
   own atomic persistence. Fault injection remains required before declaring
   the on-device migration format stable.
6. Remove the compatibility wrappers only after installer, launcher, policy,
   uninstall and data-directory call sites all use the composite identity.

The migration must preserve private data across runtime changes. Runtime type
is not part of the storage key.
