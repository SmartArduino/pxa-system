from __future__ import annotations

import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time
import uuid
from threading import Lock

from .common import KIT, ToolError, evidence, run


def info_fields(text):
    return dict(re.findall(r'(?:^|[;\s])([a-zA-Z0-9_-]+)=([^;\r\n]+)', text))


class Jobs:
    """Own each subprocess/container, with bounded logs and explicit cleanup."""
    def __init__(self, environment):
        self.env = environment
        self.jobs = {}
        self.input_locks = {}

    def start(self, arguments, *, timeout=900, entrypoint=None, host=False, headless=False):
        identity = uuid.uuid4().hex[:16]
        output = evidence(self.env.workspace)
        name = None if self.env.native or host else 'pxa-ai-' + identity
        command = self.env.invocation(arguments, container_name=name, entrypoint=entrypoint) if not host else list(map(str, arguments))
        if self.env.native and entrypoint and not host:
            command = [entrypoint, *map(str, arguments)]
        env = {**self.env.variables(), 'PYTHONUNBUFFERED': '1'}
        if headless:
            env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
            if name:
                image = command.index(self.env.config['image'])
                command[image:image] = ['-e', 'SDL_VIDEODRIVER=dummy', '-e', 'SDL_AUDIODRIVER=dummy']
        process = subprocess.Popen(command, cwd=self.env.workspace, env=env, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, start_new_session=os.name != 'nt',
            **({'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt' else {}))
        job = {'process': process, 'directory': output, 'container': name,
               'started': time.monotonic(), 'timeout': timeout, 'done': False, 'timed_out': False}
        self.jobs[identity] = job

        def capture():
            with (output / 'run.log').open('wb') as log:
                while data := os.read(process.stdout.fileno(), 65536):
                    if log.tell() + len(data) > 8 * 1024**2:
                        log.seek(0)
                        log.truncate()
                    log.write(data)
                    log.flush()
            process.wait()
            job['done'] = True

        def deadline():
            try:
                process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                job['timed_out'] = True
                self.stop(identity)

        threading.Thread(target=capture, daemon=True).start()
        threading.Thread(target=deadline, daemon=True).start()
        return self.status(identity)

    def get(self, identity):
        if identity not in self.jobs:
            raise ToolError('Unknown job; use the job ID returned by this server')
        return self.jobs[identity]

    def status(self, identity, *, wait_seconds=0, include_log=True):
        job = self.get(identity)
        if not 0 <= wait_seconds <= 20:
            raise ToolError('Job wait must be 0..20 seconds')
        if wait_seconds and job['process'].poll() is None:
            try:
                job['process'].wait(timeout=wait_seconds)
            except subprocess.TimeoutExpired:
                pass
        log = job['directory'] / 'run.log'
        text = ''
        if log.exists():
            with log.open('rb') as stream:
                stream.seek(max(0, log.stat().st_size - 16000))
                text = stream.read(16000).decode('utf-8', errors='replace')
        selectors = re.findall(r'PXADB: pxa device info --simulator ([a-z0-9@-]+)', text)
        if selectors:
            job['selector'] = selectors[-1]
        complete = job['process'].poll() is not None and job['done']
        if complete and 'scope' not in job:
            from .scope import check
            job['scope'] = check(self.env.workspace)
        return {'job_id': identity, 'running': not complete, 'scope': job.get('scope'),
                'passed': (job['process'].returncode == 0 and not job['timed_out']
                           and not job.get('scope', {}).get('violations')) if complete else None,
                'returncode': job['process'].poll(), 'timed_out': job['timed_out'],
                'elapsed_seconds': round(time.monotonic() - job['started'], 2),
                'log': str(log), 'log_tail': text if include_log else '', 'simulator': job.get('selector')}

    def stop(self, identity):
        job = self.get(identity)
        if job.get('interaction_job'):
            self.stop(job['interaction_job'])
        process = job['process']
        if process.poll() is None:
            if job['container']:
                subprocess.run([self.env.config['engine'], 'stop', '--time', '3', job['container']],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
            elif os.name == 'nt':
                process.send_signal(signal.CTRL_BREAK_EVENT)
            else:
                os.killpg(process.pid, signal.SIGINT)
            try:
                process.wait(timeout=6)
            except subprocess.TimeoutExpired:
                if os.name == 'nt':
                    subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
                else:
                    os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=6)
        return self.status(identity)

    def simulator(self, identity, arguments):
        with self.input_locks.setdefault(identity, Lock()):
            return self._simulator(identity, arguments)

    def _simulator(self, identity, arguments):
        job = self.get(identity)
        if job.get('interaction_job') and self.status(job['interaction_job'])['running']:
            raise ToolError('Wait for the interaction batch before sending other simulator commands')
        status = self.status(identity)
        selector = status['simulator']
        if not status['running'] or not selector:
            raise ToolError('The simulator is not ready; inspect this job first')
        arguments = [*map(self.env.mapped, arguments), '--simulator', selector]
        def invoke(values):
            if self.env.native:
                return self.env.device(values)
            return run([self.env.config['engine'], 'exec', job['container'], '/opt/pxa/bin/pxa', 'device', *values], timeout=60)
        # The SDK announces its selector just before the bridge binds. Wait by
        # reading INFO; never retry a potentially applied input/write operation.
        if not job.get('bridge_ready'):
            deadline = time.monotonic() + 30
            while True:
                try:
                    invoke(['info', '--simulator', selector])
                    job['bridge_ready'] = True
                    break
                except ToolError:
                    if time.monotonic() >= deadline or job['process'].poll() is not None:
                        raise
                    time.sleep(.2)
        if arguments[:2] in (['input', 'controller'], ['input', 'cancel']):
            values = [selector]
            if arguments[1] == 'controller':
                values += ['--buttons', arguments[2], '--hold-ms', arguments[4]]
            helper = KIT / 'pxa_agent/simulator_input.py'
            command = ([sys.executable, helper, *values] if self.env.native else
                [self.env.config['engine'], 'exec', job['container'], '/opt/pxa/runtime/python/bin/python3',
                 '/pxa-agent-kit/pxa_agent/simulator_input.py', *values])
            return run(command, env=self.env.variables(), timeout=40)
        return invoke(arguments)

    def interact(self, identity, steps):
        """Keep a batch ordered and reject overlapping input on its owner."""
        with self.input_locks.setdefault(identity, Lock()):
            return self._interact(identity, steps)

    def _interact(self, identity, steps):
        from .simulator_batch import validate
        # Exact profile bounds are checked inside the selected DevKit before input.
        validate(steps, 16384, 16384)
        state = self.status(identity)
        if not state['running'] or not state['simulator']:
            raise ToolError('Wait for the owned simulator before running a batch')
        owner = self.get(identity)
        if owner.get('interaction_job') and self.status(owner['interaction_job'])['running']:
            raise ToolError('An interaction batch is still running on this simulator')
        output = evidence(self.env.workspace, 'pxa-interact')
        output.rmdir()
        helper = KIT / 'pxa_agent/simulator_batch.py'
        sdk = self.env.config.get('sdk') if self.env.native else '/opt/pxa'
        arguments = ['--sdk', sdk, '--simulator', state['simulator'],
                     '--steps', json.dumps(steps), '--output', self.env.mapped(output)]
        command = ([sys.executable, helper, *arguments] if self.env.native else
            [self.env.config['engine'], 'exec', owner['container'], '/opt/pxa/runtime/python/bin/python3',
             '/pxa-agent-kit/pxa_agent/simulator_batch.py', *arguments])
        job = self.start(command, host=True, timeout=180)
        owner['interaction_job'] = job['job_id']
        return {**job, 'output':str(output), 'result':str(output / 'result.json'),
                'screenshots':[str(output / (s['name']+'.png')) for s in steps if s['action']=='screenshot'],
                'visual_review_required':True}

    def close(self):
        for identity in list(self.jobs):
            self.stop(identity)


def verification(environment, project, *, profiles=None, scenario=None, headless=True, store_screenshots=False):
    project = environment.project(project)
    # Evidence must stay outside the app; keeping apps in apps/<name> also keeps
    # screenshots out of source/resource discovery and signing inputs.
    if project == environment.workspace:
        raise ToolError('For verification, open the app parent as the workspace so evidence stays outside the app')
    environment.ensure_identity(project)
    output = evidence(environment.workspace, 'pxa-verify')
    output.rmdir()  # The verification helper requires a new destination.
    helper = KIT / 'skills/pxa-debug/scripts/verify_app.py'
    arguments = [helper, project, '--pxa',
        str(Path(environment.config['sdk']) / 'bin/pxa') if environment.native else '/opt/pxa/bin/pxa',
        '--output', output, '--timeout', '600']
    if profiles:
        arguments += ['--profiles', *profiles]
    if scenario:
        arguments += ['--scenario', environment.project(scenario)]
    if headless:
        arguments.append('--headless')
    if store_screenshots:
        arguments.append('--store-screenshots')
    if not environment.native:
        arguments[0] = '/pxa-agent-kit/skills/pxa-debug/scripts/verify_app.py'
    log = output.with_suffix('.log')
    jobs = Jobs(environment)
    try:
        state = jobs.start(arguments, entrypoint=sys.executable if environment.native else '/opt/pxa/runtime/python/bin/python3', timeout=1200)
        while state['running']:
            time.sleep(.2)
            state = jobs.status(state['job_id'])
        returncode = state['returncode']
        log.write_bytes(Path(state['log']).read_bytes())
    finally:
        jobs.close()
    path = output / 'result.json'
    from .scope import enforce
    enforce(environment.workspace)
    document = json.loads(path.read_text()) if path.is_file() else {'runtime_passed': False}
    return {'runtime_passed': returncode == 0 and document.get('runtime_passed', False),
            'capture': document.get('capture'),
            'visual_review_required': True, 'result': str(path), 'output': str(output),
            'log': str(log), 'returncode': returncode,
            'screenshots': [str(path) for path in sorted(output.rglob('*.png'))]}


def install_app(environment, project, *, port, baud=2000000):
    if not port:
        raise ToolError('Choose the target device port explicitly after discovery')
    project = environment.project(project)
    environment.ensure_identity(project)
    info = info_fields(environment.device(['info', '--port', port, '--baud', str(baud)]))
    target = info.get('target')
    if target not in ('esp32s3', 'esp32s31'):
        raise ToolError('The device did not report a supported CPU target')
    environment.call(['build', project, '--offline', '--target', target, '--aot-only'], timeout=600)
    metadata = json.loads((project / 'package.json').read_text())
    package = project / 'dist' / (metadata['id'] + '.pxa')
    environment.call(['publish', package, '--check-only'])
    inventory = environment.device(['package', 'list', '--port', port, '--baud', str(baud)])
    output = environment.device(['package', 'install', package, '--yes', '--port', port, '--baud', str(baud)], timeout=300)
    output += environment.device(['package', 'run', metadata['id'], '--port', port, '--baud', str(baud)])
    active = False
    for _ in range(30):
        inventory_after = environment.device(['package', 'list', '--port', port, '--baud', str(baud)])
        if any(f":{metadata['id']}\t" in line and 'active=1' in line for line in inventory_after.splitlines()):
            active = True
            break
        time.sleep(.25)
    if not active:
        raise ToolError('Installation completed but the requested app did not become active; inspect device logs, Core and service compatibility')
    return {'app_id': metadata['id'], 'target': target, 'board': info.get('board'),
            'package': str(package), 'device': port, 'active_verified': active, 'inventory_before': inventory, 'log': output}
