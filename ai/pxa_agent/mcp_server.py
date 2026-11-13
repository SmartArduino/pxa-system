"""Typed MCP tools backed by the same CLI and the actual PXA Guest."""
from __future__ import annotations

import base64
from contextlib import asynccontextmanager
import io
from functools import wraps
import subprocess
import json
from pathlib import Path
import sys
from typing import Literal

from mcp.server import MCPServer
from mcp.server.mcpserver.exceptions import ToolError as MCPToolError
from mcp.types import ImageContent, TextContent, ToolAnnotations
from PIL import Image

from . import VERSION
from .common import KIT, ToolError, evidence
from .workbench import Jobs, verification
from .authoring import AUTHORING_BRIEF

Profile = Literal['pai-touch', 'sensecap-watcher', 'esp-mosaico', 'esp32s31-korvo-1',
                  'doit-gamekit', 'doit-gamekit-1', 'doit-gamekit-2', 'doit-esp32s31-devkit', 'espp4-a', 'generic']


def create_server(env):
    jobs = Jobs(env)

    @asynccontextmanager
    async def lifespan(server):
        try:
            yield {}
        finally:
            jobs.close()

    server = MCPServer('pxa', version=VERSION, lifespan=lifespan,
        instructions='Implement the user goal, build, exercise real Guest input and inspect screenshots; iterate on failures. '
        'Use pxa_context for bounded local SDK excerpts. App work must not change SDK, ABI, runtime or board code. '
        'Board ports may edit only the selected BSP, profile and generated lock; keep SDK/API read-only. Use scope checks. '
        'Keep original app/signing identities and device saves. Use only user-authorized device changes, firmware updates and publishing. '
        'Long operations return a job_id: poll pxa_job_status, then inspect evidence. Runtime success is not visual or hardware acceptance. '
        'Store screenshots use the real simulator, rectangular frames without rounded masks, and hidden performance overlays and bottom navigation. Inspect originals before upload. '
        'Browser store approval happens in the user browser; never ask for private keys, passwords or tokens in chat. '
        + AUTHORING_BRIEF)
    read = ToolAnnotations(readOnlyHint=True, destructiveHint=False)

    def tool(**options):
        def register(function):
            @wraps(function)
            def invoke(*args, **kwargs):
                try:
                    return function(*args, **kwargs)
                except (ToolError, OSError, ValueError, subprocess.TimeoutExpired) as error:
                    raise MCPToolError(str(error)) from error
            return server.tool(**options)(invoke)
        return register

    def host_job(arguments, timeout=1200, reload_environment=False):
        return jobs.start([sys.executable, KIT / 'agent.py', '--workspace', env.workspace, *arguments], host=True, timeout=timeout,
                          **({'reload_environment':True} if reload_environment else {}))

    def selected(port, job_id):
        if bool(port) == bool(job_id):
            raise ToolError('Specify exactly one physical port or owned simulator job_id')

    def control(arguments, port, job_id, baud, *, timeout=60):
        selected(port, job_id)
        if job_id:
            return jobs.simulator(job_id, arguments, **({'timeout': timeout} if timeout != 60 else {}))
        return env.device([*arguments, '--port', port, '--baud', str(baud)],
                          **({'timeout': timeout} if timeout != 60 else {}))

    def image_content(path):
        path = env.project(path)
        if not path.is_file() or path.stat().st_size > 16 * 1024**2:
            raise ToolError('Choose a workspace image no larger than 16 MiB')
        with Image.open(path) as source:
            source.thumbnail((1600, 1600))
            output = io.BytesIO()
            source.convert('RGB').save(output, format='PNG')
        return [TextContent(type='text', text=str(path)),
                ImageContent(type='image', mimeType='image/png', data=base64.b64encode(output.getvalue()).decode())]

    @tool(annotations=read)
    def pxa_doctor(include_usage: bool = False) -> dict:
        """Verify installed SDK/engine and list serial ports without opening them."""
        result = env.doctor()
        if include_usage:
            from .sdk_cache import usage
            result['storage'] = usage()
        return result

    @tool()
    def pxa_sdk_cache(action: Literal['usage', 'prune'] = 'usage', dry_run: bool = True) -> dict:
        """Inspect downloads or prune archives identified by verified installed SDK
        receipts. Pruning defaults to dry-run, keeps installed SDKs, firmware and
        application data, and waits for active SDK archive readers.
        """
        from .sdk_cache import command
        return command(env, ['cache'] if action == 'usage' else
                       ['prune', *(['--dry-run'] if dry_run else [])])

    @tool()
    def pxa_sdk_repair(sdk_version: str | None = None, manifest: str | None = None,
                       offline: bool = False, keep_archives: bool = False) -> dict:
        """Explicitly reinstall a signed native SDK without migrating project locks
        or changing signing keys/saves. Stops this server's owned sessions first.
        Offline repair requires retained archives and a local signed receipt.
        A custom selected SDK is retained. Returns a job; poll for completion.
        """
        arguments = ['sdk', 'repair']
        if sdk_version: arguments += ['--sdk-version', sdk_version]
        if manifest: arguments += ['--manifest', manifest]
        if offline: arguments += ['--offline']
        if keep_archives: arguments += ['--keep-archives']
        jobs.close()
        return host_job(arguments, timeout=1800)

    @tool()
    def pxa_clean(project: str, keep: int = 2, dry_run: bool = True, aot: bool = False) -> dict:
        """Clean old app build/package caches with the SDK's build lock. Keeps
        published packages, referenced generations/configurations, keys and saves.
        Defaults to a preview; requires a DevKit whose clean command is supported.
        """
        if type(keep) is not int or not 0 <= keep <= 100:
            raise ToolError('Keep must be in 0..100')
        arguments = ['clean', env.project(project), '--keep', str(keep)]
        if dry_run: arguments += ['--dry-run']
        if aot: arguments += ['--aot']
        return jobs.start(arguments)

    @tool()
    def pxa_workspace_migrate(sdk_version: str, sdk: str | None = None,
                              engine: Literal['auto','native','docker','podman'] = 'auto',
                              manifest: str | None = None) -> dict:
        """Explicitly select another SDK, retaining app locks/signing identities and
        scope audit history. Downloads only verified distributions unless selecting
        an explicit local DevKit. Existing apps stay pinned until sdk update APP.
        Returns a job; completion reloads this server's environment.
        """
        arguments=['workspace','migrate','--sdk-version',sdk_version,'--engine',engine]
        if sdk:arguments += ['--sdk',str(Path(sdk).expanduser().resolve())]
        if manifest:arguments += ['--manifest',manifest]
        jobs.close()  # Finish owned SDK sessions before changing their environment.
        return host_job(arguments,timeout=1800,reload_environment=True)

    @tool(annotations=read)
    def pxa_device_profile(port: str | None = None, baud: int = 2000000, package: str | None = None, serial_profile: str | None = None) -> dict:
        """Read device resolution, CPU, debug inputs and sensor descriptors; without a port list candidates and PXADB setup steps. Confirm physical controls and adaptive layout with the user."""
        from .device_profile import inspect
        return inspect(env, port, baud, package, serial_profile)

    @tool(annotations=read)
    def pxa_check_compatibility(package: str, port: str, baud: int = 2000000) -> dict:
        """Compare a prepared binary package with actual Host Core, services/features,
        CPU, AOT ABI and container support without uploading. Unknown telemetry stays
        unknown; device signature/permissions/activation remain authoritative.
        """
        from .compatibility import inspect
        return inspect(env,package,port=port,baud=baud)

    @tool()
    def pxa_init(project: str, app_id: str | None = None, language: Literal['c', 'cpp'] = 'cpp') -> dict:
        """Create a C/C++ app and start its change scope; never replace an existing app."""
        from .projects import initialize
        return initialize(env, project, app_id, language)

    @tool(annotations=read)
    def pxa_recipes() -> dict:
        """List tested app starters and their minimum compiler SDK; no migration."""
        from .recipes import catalog
        return catalog()

    @tool()
    def pxa_new(project: str, recipe: Literal['timer','counter'], title: str | None = None,
                app_id: str | None = None, minutes: int | None = None,
                break_minutes: int | None = None) -> dict:
        """Create a new recipe app with adaptive safe layout, controller/touch,
        checkpoint storage and semantic journey. Requires selected DevKit0.10+;
        never replaces an existing app or changes SDK locks implicitly.
        Creation is not runtime acceptance: verify its journey and inspect images.
        """
        from .recipes import create
        return create(env,project,recipe,title=title,app_id=app_id,minutes=minutes,break_minutes=break_minutes)

    @tool(annotations=read)
    def pxa_context(project: str, query: str = '', language: Literal['c', 'cpp'] = 'cpp', limit: int = 12) -> dict:
        """Read compact app metadata, SDK lock, exact header lines and examples; no network or source edits."""
        from .context import project_context
        return project_context(env, project, query, language, limit)

    @tool()
    def pxa_scope_begin(mode: Literal['app', 'board'], project: str | None = None, board: str | None = None,
                        adopt: bool = False) -> dict:
        """Record change boundaries before editing. Does not grant permission or discard existing changes."""
        from .scope import begin
        return begin(env.workspace, mode=mode, project=project, board=board,
                     sdk=env.config.get('sdk') if mode == 'app' else None, adopt=adopt)

    @tool(annotations=read)
    def pxa_scope_check() -> dict:
        """Check file changes outside the selected app/BSP. This audit is not an OS sandbox."""
        from .scope import check
        return check(env.workspace)

    @tool()
    def pxa_scope_end(allow_violations: bool = False) -> dict:
        """Archive the state and audit without changing source; failed audits retain passed=false and violations."""
        from .scope import finish
        return finish(env.workspace, allow_violations=allow_violations)

    @tool()
    def pxa_board_init(board: str, target: Literal['esp32s3', 'esp32s31', 'esp32p4'], reference: str | None = None) -> dict:
        """Create board-local callback scaffolding with a bring-up gate; never guess GPIO or flash hardware."""
        from .boards import initialize
        return initialize(env.workspace, board, target=target, reference=reference)

    @tool(annotations=read)
    def pxa_board_check(board: str) -> dict:
        """Inspect existing port API, missing hardware data and forbidden edits; not hardware acceptance."""
        from .boards import inspect
        return inspect(env.workspace, board)

    @tool()
    def pxa_board_build(board: str) -> dict:
        """Build the selected board after boundary/static checks, using its installed ESP-IDF. Never flash."""
        return host_job(['board', 'build', board], timeout=1900)

    @tool()
    def pxa_build(project: str, target: Literal['simulator', 'windows-simulator', 'esp32s3', 'esp32s31', 'esp32p4', 'wasm'] = 'simulator',
                  targets: list[Literal['simulator', 'windows-simulator', 'esp32s3', 'esp32s31', 'esp32p4', 'wasm']] | None = None) -> dict:
        """Build offline with the pinned SDK/signing identity. Multiple targets produce a universal
        container; use pxa_store_build for separate per-target store packages sharing build caches.
        Target remains the single-target shortcut. Returns a job.
        """
        from .projects import build_targets
        selected_targets = build_targets(target, targets)
        path = env.project(project)
        env.ensure_identity(path)
        return jobs.start(['build', path, '--target', selected_targets, '--offline'])

    @tool()
    def pxa_run(project: str, profile: Profile = 'pai-touch', headless: bool = True) -> dict:
        """Start the real Guest/debug bridge; use this job_id for screenshots and input. Stop it when finished."""
        path = env.project(project)
        env.ensure_identity(path)
        return jobs.start(['run', path, '--offline', '--debug', '--profile', profile], timeout=3600, headless=headless)

    @tool(annotations=read)
    def pxa_job_status(job_id: str, wait_seconds: int = 0, include_log: bool = True) -> dict:
        """Wait up to 20 seconds and return completion/scope. Poll with include_log=false; read logs on failure/completion."""
        return jobs.status(job_id, wait_seconds=wait_seconds, include_log=include_log)

    @tool()
    def pxa_job_stop(job_id: str) -> dict:
        """Stop this server's job. Batch cancellation releases input and keeps its simulator available;
        an unresponsive worker forces its owned simulator to stop. Never replay cancelled steps.
        """
        return jobs.stop(job_id)

    @tool()
    def pxa_interact(job_id: str, steps: list[dict]) -> dict:
        """Run 1..32 ordered input, semantic UI assertions and screenshots on this owned simulator.

        Each step has unique name and action. Tap: x,y; swipe: x,y,x2,y2,ms;
        controller: buttons,ms; wait: ms. Up to 8 screenshots and 15 seconds of
        holds/waits. Semantic actions: tap-node {selector}, expect {selector,expected},
        wait-for {selector,expected,timeout_ms}; selector uses id/surface/type/text.
        Wait timeouts count toward the 15-second budget. Returns a job and evidence paths. Poll pxa_job_status for the
        final batch result, then inspect images. Use pxa_job_stop to cancel.
        """
        return jobs.interact(job_id, steps)

    @tool()
    def pxa_verify(project: str, profiles: list[Profile] | None = None, scenario: str | None = None,
                   store_screenshots: bool = False, definitions: list[str] | None = None,
                   require_ui: bool = False, no_build: bool = False, profile_jobs: int = 2,
                   resource_package: str | None = None) -> dict:
        """Run real Guest checks. Use require_ui=true for retained-UI apps and scenario
        expect/wait-for steps for behavior; Surface-only apps verify actual frames separately.
        Store mode captures rectangular frames with navigation hidden. Disable app-owned
        performance overlays and inspect every original PNG.
        resource_package preloads the exact signed resource generation bound by the app
        into its isolated read-only media root; code and resources need the same signer.
        """
        path = env.project(project)
        env.ensure_identity(path)
        arguments = ['verify', str(path)]
        if not 1 <= profile_jobs <= 4:
            raise ToolError('profile_jobs must be 1..4')
        arguments += ['--jobs', str(profile_jobs)]
        if no_build:
            arguments.append('--no-build')
        if profiles:
            arguments += ['--profiles', *profiles]
        if scenario:
            arguments += ['--scenario', str(env.project(scenario))]
        if resource_package:
            arguments += ['--resource-package', str(env.project(resource_package))]
        if store_screenshots:
            arguments.append('--store-screenshots')
        for definition in definitions or []:
            arguments += ['--define', definition]
        if require_ui:
            arguments.append('--require-ui')
        return host_job(arguments)

    @tool(annotations=read)
    def pxa_view_image(path: str) -> list[TextContent | ImageContent]:
        """Read a workspace screenshot as MCP image content for actual visual review."""
        return image_content(path)

    @tool(annotations=read)
    def pxa_info(port: str | None = None, job_id: str | None = None, baud: int = 2000000) -> dict:
        """Read INFO from one selected device or owned simulator; identify board and CPU before installation."""
        return {'log': control(['info'], port, job_id, baud)}

    @tool(annotations=read)
    def pxa_ui_inspect(port: str | None = None, job_id: str | None = None, baud: int = 2000000,
                       limit: int = 64, offset: int = 0) -> dict:
        """Read the actual retained Guest UI: node IDs, parents, text, values and pixel bounds.
        Password inputs are redacted. Geometry does not prove a node is unobscured;
        inspect screenshots too. Returns up to 64 nodes by default (max 256), with
        the full snapshot saved locally. Offset pages are fresh observations;
        compare snapshot hashes. Unsupported older Hosts fail explicitly.
        """
        from .ui import inspection_page
        if type(limit) is not int or not 1 <= limit <= 256 or type(offset) is not int or not 0 <= offset <= 4096:
            raise ToolError('UI inspection accepts limit 1..256 and offset 0..4096')
        tree = json.loads(control(['ui', 'inspect'], port, job_id, baud))
        return inspection_page(tree, evidence(env.workspace, 'pxa-ui') / 'tree.json', limit=limit, offset=offset)

    @tool(annotations=read)
    def pxa_ui_expect(selector: dict, expected: dict | None = None, timeout_ms: int = 0,
                      port: str | None = None, job_id: str | None = None, baud: int = 2000000) -> dict:
        """Assert real UI state, optionally waiting up to 60000 ms. Selector: id/surface/type/exact text.
        Expected: visible (default true), enabled, value, count (default one), within [x,y,w,h].
        Ambiguous matches and failed conditions are errors, never implicit passes.
        """
        from .ui import query_arguments
        arguments = query_arguments('wait-for' if timeout_ms else 'expect', selector, expected, timeout_ms)
        return json.loads(control(arguments, port, job_id, baud, timeout=max(60, timeout_ms / 1000 + 10)))

    @tool()
    def pxa_tap_node(selector: dict, port: str | None = None, job_id: str | None = None, baud: int = 2000000) -> dict:
        """Resolve exactly one visible enabled control and inject a real pointer tap.
        A text selector can resolve its clickable ancestor. Verify the resulting state with
        pxa_ui_expect; overlays may intercept pointer input. Never chooses an arbitrary match.
        """
        from .ui import query_arguments
        return json.loads(control(query_arguments('tap', selector), port, job_id, baud))

    @tool(annotations=read)
    def pxa_packages(port: str, baud: int = 2000000) -> dict:
        """Read installed device apps before updating; no uninstall or storage clearing."""
        return {'log': env.device(['package', 'list', '--port', port, '--baud', str(baud)])}

    @tool(annotations=read)
    def pxa_screenshot(port: str | None = None, job_id: str | None = None, baud: int = 2000000) -> list[TextContent | ImageContent]:
        """Capture the actual device/Guest framebuffer, save local evidence and return the image."""
        path = evidence(env.workspace, 'pxa-screen') / 'screen.png'
        control(['screenshot', str(path)], port, job_id, baud)
        return image_content(path)

    @tool()
    def pxa_tap(x: int, y: int, port: str | None = None, job_id: str | None = None, baud: int = 2000000) -> dict:
        """Inject a tap at actual screenshot pixel coordinates; inspect the result afterward."""
        if x < 0 or y < 0:
            raise ToolError('Coordinates must be nonnegative')
        return {'log': control(['input', 'tap', str(x), str(y)], port, job_id, baud)}

    @tool()
    def pxa_swipe(x: int, y: int, x2: int, y2: int, duration_ms: int = 300,
                  port: str | None = None, job_id: str | None = None, baud: int = 2000000) -> dict:
        """Inject a bounded swipe using actual screen pixels."""
        if min(x, y, x2, y2) < 0 or not 1 <= duration_ms <= 5000:
            raise ToolError('Invalid swipe coordinates or duration')
        return {'log': control(['input', 'swipe', str(x), str(y), str(x2), str(y2), '--duration-ms', str(duration_ms)], port, job_id, baud)}

    @tool()
    def pxa_controller(buttons: int, hold_ms: int = 100, port: str | None = None,
                       job_id: str | None = None, baud: int = 2000000) -> dict:
        """Inject a controller bitmask and automatically release it; use the SDK ControllerButton enum for masks."""
        if not 0 <= buttons <= 4095 or not 1 <= hold_ms <= 3000:
            raise ToolError('Controller mask must be 0..4095 and hold 1..3000 ms')
        return {'log': control(['input', 'controller', str(buttons), '--hold-ms', str(hold_ms)], port, job_id, baud)}

    @tool()
    def pxa_input_cancel(port: str | None = None, job_id: str | None = None, baud: int = 2000000) -> dict:
        """Cancel synthetic input and restore physical GPIO input reporting after tests."""
        return {'log': control(['input', 'cancel'], port, job_id, baud)}

    @tool(annotations=read)
    def pxa_memory(port: str | None = None, job_id: str | None = None, baud: int = 2000000) -> dict:
        """Read supported runtime memory diagnostics; unavailable capability is not a measurement."""
        return {'log': control(['memory'], port, job_id, baud)}

    @tool()
    def pxa_install(project: str, port: str, baud: int = 2000000, strict_compatibility: bool = False) -> dict:
        """Build for the INFO CPU, verify signature, install and run an authorized app update; preserve private data."""
        return host_job(['install', str(env.project(project)), '--port', port, '--baud', str(baud),
                         *(['--strict-compatibility'] if strict_compatibility else [])], timeout=900)

    @tool(annotations=read)
    def pxa_firmware_status(port: str, board: str | None = None, release: str = 'latest', baud: int = 2000000) -> dict:
        """Read device INFO and signed release metadata without downloading an image,
        resetting or flashing. Partition match and running image origin remain unverified.
        Returns a job; poll pxa_job_status for the report.
        """
        arguments = ['firmware','status','--port',port,'--release',release,'--baud',str(baud)]
        if board:
            arguments += ['--board',board]
        return host_job(arguments,timeout=180)

    @tool(annotations=read)
    def pxa_firmware_prepare(board: str, release: str = 'latest', mode: Literal['app', 'full'] = 'app') -> dict:
        """Download and verify official signed firmware without opening a device; returns a job."""
        return host_job(['firmware', 'prepare', '--board', board, '--release', release, '--mode', mode])

    @tool()
    def pxa_firmware_upgrade(board: str, port: str, release: str = 'latest', baud: int = 2000000,
                             mode: Literal['app', 'full'] = 'app', erase_data_authorized: bool = False) -> dict:
        """Perform a user-authorized official firmware update. App mode checks actual partitions before writing.
        Full mode erases installed apps/saves/settings: set erase_data_authorized only with explicit user authorization.
        """
        arguments = ['firmware', 'upgrade', '--board', board, '--port', port, '--release', release,
                     '--baud', str(baud), '--mode', mode]
        if mode == 'full' and not erase_data_authorized:
            raise ToolError('Full flashing requires explicit user authorization to lose data')
        if erase_data_authorized:
            arguments += ['--erase-data']
        return host_job(arguments, timeout=1200)

    @tool()
    def pxa_store_build(project: str,
                        targets: list[Literal['esp32s3', 'esp32s31', 'esp32p4', 'simulator', 'windows-simulator', 'wasm']],
                        target_timeout_seconds: int = 1800) -> dict:
        """Build separate signed store packages; always adds portable Wasm to requested native targets. Reuses pinned SDK
        compilation/resource caches, retains app identity and returns a checked packages.json.
        Each target has a bounded 1..3600-second deadline; the job budget covers the full matrix.
        Verify requested runtime behavior before pxa_store_publish_targets. Never merges architectures.
        """
        from .projects import build_targets
        if type(target_timeout_seconds) is not int or not 1 <= target_timeout_seconds <= 3600:
            raise ToolError('target_timeout_seconds must be 1..3600')
        selected = build_targets(targets=targets).split(',')
        count = len(set(selected) | {'wasm'})
        return host_job(['store', 'build', str(env.project(project)), '--targets', *selected,
                         '--target-timeout-seconds', str(target_timeout_seconds)],
                        timeout=count * target_timeout_seconds + 300)

    @tool()
    def pxa_store_publish_targets(project: str, manifest: str, server_origin: str = 'https://app.doit.am',
                                  icon: str | None = None, cover: str | None = None,
                                  screenshots: list[str] | None = None, skip_icon: bool = False,
                                  skip_cover: bool = False, changelog: str = '') -> dict:
        """Submit separate target variants of one authorized app/version for review. Validates all
        signed packages first, requires and uploads the separate Wasm package first, uploads media once and reuses the remembered account. Partial failure
        lists completed and unfinished targets; inspect store state instead of replaying the batch.
        """
        arguments = ['store', 'publish-targets', str(env.project(project)), str(env.project(manifest)),
                     '--server', server_origin, '--changelog', changelog]
        for flag, value in (('--icon', icon), ('--cover', cover)):
            if value:
                arguments += [flag, str(env.project(value))]
        if skip_icon:
            arguments.append('--skip-icon')
        if skip_cover:
            arguments.append('--skip-cover')
        for value in screenshots or []:
            arguments += ['--screenshot', str(env.project(value))]
        return host_job(arguments, timeout=1500)

    @tool()
    def pxa_store_authorize(project: str, package: str, server_origin: str = 'https://app.doit.am', renew: bool = False,
                            app_only: bool = False) -> dict:
        """Verify signing-key possession and reuse the remembered store account for this app.
        First use opens browser approval; app_only requests legacy one-app approval.
        Read credential_ref; private keys and account/upload credentials never enter output.
        """
        return host_job(['store', 'authorize', str(env.project(project)), str(env.project(package)), '--server', server_origin,
                         *(['--renew'] if renew else []), *(['--app-only'] if app_only else [])])

    @tool()
    def pxa_store_login(server_origin: str = 'https://app.doit.am', switch_account: bool = False) -> dict:
        """Remember this computer's publishing account after one browser approval. Poll credential_ref.
        Reuses an existing account; switch_account signs out first. Returns no credentials.
        """
        return host_job(['store', 'login', '--server', server_origin, *(['--renew'] if switch_account else [])])

    @tool(annotations=read)
    def pxa_store_account(server_origin: str = 'https://app.doit.am') -> dict:
        """Show the remembered publishing account and local credential storage, without secrets."""
        from .store import account_status
        return account_status(server_origin)

    @tool()
    def pxa_store_logout(server_origin: str = 'https://app.doit.am') -> dict:
        """Revoke this computer's publishing authority and remove its local store credentials."""
        return host_job(['store', 'logout', '--server', server_origin])

    @tool(annotations=read)
    def pxa_store_status(credential_ref: str) -> dict:
        """Poll browser approval at the server-advertised interval; returns no upload token."""
        from .store import status
        return status(credential_ref)

    @tool()
    def pxa_store_publish(project: str, package: str, icon: str | None = None,
                          screenshots: list[str] | None = None, changelog: str = '',
                          server_origin: str = 'https://app.doit.am', cover: str | None = None,
                          skip_icon: bool = False, skip_cover: bool = False, allow_universal: bool = False) -> dict:
        """Submit an authorized single-target release and optional media for review. Multi-target
        packages require explicit allow_universal. Native uploads require a separate Wasm package already
        uploaded for the same version/sequence/signer; prefer pxa_store_build/publish_targets.
        Explicit listing text/taxonomy/media edits create a review draft; approved listings stay live.
        Inspect listing_update/media_updates for review status; pending does not request withdrawal.
        """
        arguments = ['store', 'publish', str(env.project(project)), str(env.project(package)),
                     '--server', server_origin, '--changelog', changelog]
        if icon:
            arguments += ['--icon', str(env.project(icon))]
        if cover:
            arguments += ['--cover', str(env.project(cover))]
        if skip_icon:
            arguments += ['--skip-icon']
        if skip_cover:
            arguments += ['--skip-cover']
        if allow_universal:
            arguments += ['--allow-universal']
        for path in screenshots or []:
            arguments += ['--screenshot', str(env.project(path))]
        return host_job(arguments, timeout=600)

    @tool(annotations=read)
    def pxa_store_prepare(project: str, icon: str | None = None, cover: str | None = None,
                          screenshots: list[str] | None = None, skip_icon: bool = False,
                          skip_cover: bool = False, server_origin: str = 'https://app.doit.am') -> dict:
        """Read current media specs, categories and tags; validate local images without uploading.
        For needs_choice, offer generation if available, user-supplied files, or explicit skip.
        """
        from .store import prepare
        return prepare(env, project, server=server_origin, icon=icon, cover=cover,
                       screenshots=screenshots or (), skip_icon=skip_icon, skip_cover=skip_cover)

    return server


def serve(env):
    create_server(env).run(transport='stdio')
