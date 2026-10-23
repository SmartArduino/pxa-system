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

Profile = Literal['pai-touch', 'sensecap-watcher', 'esp-mosaico', 'esp32s31-korvo-1', 'generic']


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
        'Browser store approval happens in the user browser; never ask for private keys, passwords or tokens in chat.')
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

    def host_job(arguments, timeout=1200):
        return jobs.start([sys.executable, KIT / 'agent.py', '--workspace', env.workspace, *arguments], host=True, timeout=timeout)

    def selected(port, job_id):
        if bool(port) == bool(job_id):
            raise ToolError('Specify exactly one physical port or owned simulator job_id')

    def control(arguments, port, job_id, baud):
        selected(port, job_id)
        if job_id:
            return jobs.simulator(job_id, arguments)
        return env.device([*arguments, '--port', port, '--baud', str(baud)])

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
    def pxa_doctor() -> dict:
        """Verify installed SDK/engine and list serial ports without opening them."""
        return env.doctor()

    @tool()
    def pxa_init(project: str, app_id: str | None = None, language: Literal['c', 'cpp'] = 'cpp') -> dict:
        """Create a C/C++ app and start its change scope; never replace an existing app."""
        from .projects import initialize
        return initialize(env, project, app_id, language)

    @tool(annotations=read)
    def pxa_context(project: str, query: str = '', language: Literal['c', 'cpp'] = 'cpp', limit: int = 12) -> dict:
        """Read compact app metadata, SDK lock, exact header lines and examples; no network or source edits."""
        from .context import project_context
        return project_context(env, project, query, language, limit)

    @tool()
    def pxa_scope_begin(mode: Literal['app', 'board'], project: str | None = None, board: str | None = None) -> dict:
        """Record change boundaries before editing. Does not grant permission or discard existing changes."""
        from .scope import begin
        return begin(env.workspace, mode=mode, project=project, board=board,
                     sdk=env.config.get('sdk') if mode == 'app' else None)

    @tool(annotations=read)
    def pxa_scope_check() -> dict:
        """Check file changes outside the selected app/BSP. This audit is not an OS sandbox."""
        from .scope import check
        return check(env.workspace)

    @tool()
    def pxa_scope_end() -> dict:
        """Archive a passing boundary check before unrelated work; violations cannot be discarded."""
        from .scope import finish
        return finish(env.workspace)

    @tool()
    def pxa_board_init(board: str, target: Literal['esp32s3', 'esp32s31'], reference: str | None = None) -> dict:
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
    def pxa_build(project: str, target: Literal['simulator', 'esp32s3', 'esp32s31'] = 'simulator') -> dict:
        """Build offline with the pinned SDK and original signing identity; returns a job."""
        path = env.project(project)
        env.ensure_identity(path)
        return jobs.start(['build', path, '--target', target, '--offline'])

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
        """Stop only this server's job and its owned Guest/container."""
        return jobs.stop(job_id)

    @tool()
    def pxa_verify(project: str, profiles: list[Profile] | None = None, scenario: str | None = None) -> dict:
        """Run repeatable multi-screen Guest checks. Read result.json and inspect screenshots after the job finishes."""
        path = env.project(project)
        env.ensure_identity(path)
        arguments = ['verify', str(path)]
        if profiles:
            arguments += ['--profiles', *profiles]
        if scenario:
            arguments += ['--scenario', str(env.project(scenario))]
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
    def pxa_install(project: str, port: str, baud: int = 2000000) -> dict:
        """Build for the INFO CPU, verify signature, install and run an authorized app update; preserve private data."""
        return host_job(['install', str(env.project(project)), '--port', port, '--baud', str(baud)], timeout=900)

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
    def pxa_store_authorize(project: str, package: str, server_origin: str = 'https://app.doit.am', renew: bool = False) -> dict:
        """Verify local signing-key possession and open browser app-scoped approval. Read job output for credential_ref.
        User login/approval occurs only in the browser; keys, login cookies and upload tokens never enter tool output.
        """
        return host_job(['store', 'authorize', str(env.project(project)), str(env.project(package)), '--server', server_origin, *(['--renew'] if renew else [])])

    @tool(annotations=read)
    def pxa_store_status(credential_ref: str) -> dict:
        """Poll browser approval at the server-advertised interval; returns no upload token."""
        from .store import status
        return status(credential_ref)

    @tool()
    def pxa_store_publish(project: str, package: str, icon: str | None = None,
                          screenshots: list[str] | None = None, changelog: str = '',
                          server_origin: str = 'https://app.doit.am') -> dict:
        """Submit the user's authorized signed release and optional images for store review. Does not bypass review."""
        arguments = ['store', 'publish', str(env.project(project)), str(env.project(package)),
                     '--server', server_origin, '--changelog', changelog]
        if icon:
            arguments += ['--icon', str(env.project(icon))]
        for path in screenshots or []:
            arguments += ['--screenshot', str(env.project(path))]
        return host_job(arguments, timeout=600)

    return server


def serve(env):
    create_server(env).run(transport='stdio')
