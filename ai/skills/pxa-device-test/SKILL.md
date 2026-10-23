---
name: pxa-device-test
description: Validate a PXA app on an explicitly selected physical device using PXADB installation, input, screenshots and memory diagnostics; diagnose Core/AOT compatibility and preserve device apps, settings and saves.
---

# PXA physical-device validation

When Agent Kit setup is present, use workspace `.pxa/pxa` (Windows `.pxa/pxa.cmd`) instead of bare `pxa`; MCP tools expose the same implementation. Apps belong in `apps/<name>`. Start with `doctor`; do not rebuild the firmware environment for an app task. Follow the user goal through actual runtime and visual checks, then iterate on feedback. Use `.pxa/pxa install <app> --port <port>` for host-side USB, including when compilation runs in a container. For an authorized official firmware update, use `firmware prepare` then `firmware upgrade`; app mode checks the real partition table, full mode requires explicit data-loss authorization.

Use the port and baud requested by the user; query `pxa device info --port <port> --baud <baud>` before assuming the board, Core, services or memory capability. Device paths can change after a reconnect. If a port was not supplied, discover endpoints and identify the target before mutating a device. Do not hard-code a developer's /dev/ttyACM number into project configuration.

Record `pxa device package list` and the visible screen first. Select the target from its actual CPU: ESP32-S3 and ESP32-S31 use different AOT; desktop screen profiles do not change that CPU. Build with `pxa build <project> --target <target>` and check the signed package with `pxa publish <package> --check-only`.

Within device-test authorization, `pxa run <project> --device <port> --baud <baud>` builds, installs and starts the app. Read `pxa device input --help`, then inject the relevant tap/swipe and capture `pxa device screenshot <file> --port <port> --baud <baud>`. Verify the actual expected screen, repeat start/stop and the failing interaction, and collect memory if INFO advertises it. Note lock screens and overlays: input into a locked screen does not demonstrate app input failure. Confirm the unlocked frame before launching; a successful swipe command alone is insufficient. Match the device's gesture timing rather than repeatedly assuming a fast swipe unlocked it.

Do not format storage, clear data or update firmware as a routine app retry. Existing user authorization for a required firmware update applies; otherwise explain the specific compatibility blocker before requesting that separate action. Use the board's application-only update procedure when available, preserve data partitions and restore temporary display/audio/power settings.

For a unique test app, stop and uninstall only that test identity after collecting evidence. For an existing app, preserve its saves and installed identity; do not uninstall it as cleanup. Report the final device state, screenshots, current/peak memory boundaries and any latency that was not physically measured. Failed package checks are not fixed by disabling signature verification.
