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

from .common import KIT, ToolError, digest, evidence, run


def info_fields(text):
    return dict(re.findall(r'(?:^|[;\s])([a-zA-Z0-9_-]+)=([^;\r\n]+)', text))


class Jobs:
    """Own each subprocess/container, with bounded logs and explicit cleanup."""
    def __init__(self, environment):
        self.env = environment
        self.jobs = {}
        self.input_locks = {}

    def start(self, arguments, *, timeout=900, entrypoint=None, host=False, headless=False, reload_environment=False):
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
               'reload_environment':reload_environment,
               'started': time.monotonic(), 'timeout': timeout, 'done': False, 'timed_out': False,
               'discover_selector': not host and bool(arguments) and str(arguments[0]) == 'run'}
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
            process.stdout.close()
            job['finished'] = time.monotonic()
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
        if log.exists() and (include_log or job.get('discover_selector', True) and not job.get('selector')):
            stat = log.stat()
            fingerprint = (stat.st_ino, stat.st_size, stat.st_mtime_ns)
            if job.get('log_fingerprint') != fingerprint:
                with log.open('rb') as stream:
                    stream.seek(max(0, stat.st_size - 16000))
                    job['log_tail'] = stream.read(16000).decode('utf-8', errors='replace')
                job['log_fingerprint'] = fingerprint
            text = job.get('log_tail', '')
        selectors = re.findall(r'PXADB: pxa device info --simulator ([a-z0-9@-]+)', text)
        if selectors:
            job['selector'] = selectors[-1]
        complete = job['process'].poll() is not None and job['done']
        if complete and 'scope' not in job:
            from .scope import check
            job['scope'] = check(self.env.workspace)
        if complete and job.get('reload_environment') and not job.get('environment_reloaded') and (
                job['process'].returncode == 0 and not job['timed_out'] and not job.get('cancel_requested')
                and not job.get('scope',{}).get('violations')):
            from .environment import Environment
            current=Environment(self.env.workspace)
            self.env.config,self.env.native=current.config,current.native
            job['environment_reloaded']=True
        batch = None
        if complete and job.get('batch_output'):
            result_path = job['batch_output'] / 'result.json'
            try:
                if result_path.stat().st_size <= 2 * 1024**2:
                    batch = json.loads(result_path.read_text())
                    if not isinstance(batch, dict):
                        batch = None
                    elif result_path.stat().st_size > 65536:
                        # A valid semantic batch may contain many matching
                        # nodes. Keep its status truthful without sending the
                        # complete diagnostic tree back to the model.
                        batch = {**batch, 'details_file': str(result_path), 'details_summarized': True}
                        batch['steps'] = [{**{k: step[k] for k in
                            ('name', 'action', 'passed', 'elapsed_ms') if k in step},
                            **({'error': str(step['error'])[:2000]} if 'error' in step else {}),
                            **({'result': {k: step['result'][k] for k in
                               ('passed', 'matched_count', 'x', 'y') if k in step['result']}}
                               if isinstance(step.get('result'), dict) else {})}
                            for step in batch.get('steps', [])]
            except (OSError, ValueError):
                pass
        return {'job_id': identity, 'running': not complete, 'scope': job.get('scope'),
                'passed': (job['process'].returncode == 0 and not job['timed_out']
                           and not job.get('cancel_requested') and not job.get('scope', {}).get('violations')
                           and (not job.get('batch_output') or bool(batch and batch.get('runtime_passed')
                                and batch.get('input_released') and not batch.get('cancelled')))) if complete else None,
                'returncode': job['process'].poll(), 'timed_out': job['timed_out'],
                'cancelled': bool(job.get('cancel_requested') or batch and batch.get('cancelled')),
                **({'environment_reloaded':bool(job.get('environment_reloaded'))} if job.get('reload_environment') else {}),
                **({'batch':batch} if complete and job.get('batch_output') else {}),
                **({'cancellation_error':job['cancellation_error']} if job.get('cancellation_error') else {}),
                'elapsed_seconds': round(job.get('finished', time.monotonic()) - job['started'], 2),
                'log': str(log), 'log_tail': text if include_log else '', 'simulator': job.get('selector')}

    def stop(self, identity):
        job = self.get(identity)
        if job.get('interaction_job'):
            self.stop(job['interaction_job'])
        process = job['process']
        if job.get('cancel_file') and process.poll() is None:
            # Killing docker/podman exec only disconnects the host client. Ask
            # the worker inside the owned container to stop and release input.
            job['cancel_requested'] = True
            job['cancel_file'].touch()
            try:
                process.wait(timeout=35)
            except subprocess.TimeoutExpired:
                job['cancellation_error'] = 'Worker did not acknowledge cancellation; stopped its owned simulator'
                owner = self.get(job['batch_owner'])
                owner['cancel_requested'] = True
                self._terminate(owner)
                self._terminate(job)
        else:
            self._terminate(job)
        return self.status(identity)

    def _terminate(self, job):
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

    def simulator(self, identity, arguments, *, timeout=60):
        with self.input_locks.setdefault(identity, Lock()):
            return self._simulator(identity, arguments, timeout=timeout)

    def _simulator(self, identity, arguments, *, timeout=60):
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
                return self.env.device(values, **({'timeout': timeout} if timeout != 60 else {}))
            return run([self.env.config['engine'], 'exec', job['container'], '/opt/pxa/bin/pxa', 'device', *values], timeout=timeout)
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
        cancel_file = output.with_suffix('.cancel')
        sdk = self.env.config.get('sdk') if self.env.native else '/opt/pxa'
        arguments = ['--sdk', sdk, '--simulator', state['simulator'],
                     '--steps', json.dumps(steps), '--output', self.env.mapped(output),
                     '--cancel-file', self.env.mapped(cancel_file)]
        command = ([sys.executable, helper, *arguments] if self.env.native else
            [self.env.config['engine'], 'exec', owner['container'], '/opt/pxa/runtime/python/bin/python3',
             '/pxa-agent-kit/pxa_agent/simulator_batch.py', *arguments])
        job = self.start(command, host=True, timeout=180)
        self.get(job['job_id']).update(batch_output=output, cancel_file=cancel_file, batch_owner=identity)
        owner['interaction_job'] = job['job_id']
        return {**job, 'output':str(output), 'result':str(output / 'result.json'),
                'screenshots':[str(output / (s['name']+'.png')) for s in steps if s['action']=='screenshot'],
                'visual_review_required':True}

    def close(self):
        for identity in list(self.jobs):
            self.stop(identity)


def verification(environment, project, *, profiles=None, scenario=None, headless=True, store_screenshots=False,
                 definitions=(), require_ui=False, no_build=False, profile_jobs=2):
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
        str(Path(environment.config['sdk']) / ('bin/pxa.cmd' if os.name == 'nt' else 'bin/pxa')) if environment.native else '/opt/pxa/bin/pxa',
        '--output', output, '--timeout', '600', '--jobs', str(profile_jobs)]
    if no_build:
        arguments.append('--no-build')
    if profiles:
        arguments += ['--profiles', *profiles]
    if scenario:
        arguments += ['--scenario', environment.project(scenario)]
    if headless:
        arguments.append('--headless')
    if store_screenshots:
        arguments.append('--store-screenshots')
    for definition in definitions:
        arguments += ['--define', definition]
    if require_ui:
        arguments.append('--require-ui')
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
            'build': document.get('build'), 'preview': document.get('preview'),
            'screenshots': [str(output / item['path']) for item in document.get('screenshots', [])]}


def install_app(environment, project, *, port, baud=2000000, strict_compatibility=False):
    if not port:
        raise ToolError('Choose the target device port explicitly after discovery')
    project = environment.project(project)
    environment.ensure_identity(project)
    info = info_fields(environment.device(['info', '--port', port, '--baud', str(baud)]))
    target = {'esp32-s3':'esp32s3','esp32-s31':'esp32s31','esp32-p4':'esp32p4'}.get(info.get('target'),info.get('target'))
    if target not in ('esp32s3', 'esp32s31', 'esp32p4'):
        raise ToolError('The device did not report a supported CPU target')
    environment.call(['build', project, '--offline', '--target', target, '--aot-only'], timeout=600)
    metadata = json.loads((project / 'package.json').read_text())
    package = project / 'dist' / (metadata['id'] + '.pxa')
    package_digest = digest(package)
    environment.call(['publish', package, '--check-only'])
    from .device_session import install_connection
    with install_connection(environment, port, baud, info) as device:
        from .compatibility import inspect, admit
        compatibility = inspect(environment,package,port=port,baud=baud,device=device)
        admit(compatibility,strict=strict_compatibility)
        from .host_tools import reader
        identity = reader(environment=environment).package_composite_identity(package)
        if identity.rsplit(':',1)[-1] != metadata['id'] or digest(package) != package_digest:
            raise ToolError('Verified package changed; nothing was stopped or uploaded')
        def target_state(text):
            rows = [line.split('\t') for line in text.splitlines() if line.split('\t')[0] == identity]
            if len(rows)>1:
                raise ToolError('Multiple target installations; select its original volume before updating')
            if not rows:
                return False
            if len(rows[0]) != 4:
                raise ToolError('Malformed target inventory; nothing was uploaded')
            flags = set(rows[0][3].split(';'))
            if 'ambiguous=1' in flags:
                raise ToolError('Ambiguous target installation; select its original volume before updating')
            return 'active=1' in flags

        def wait_active(expected):
            for _ in range(30):
                if target_state(device('package','list')) == expected:
                    return True
                time.sleep(.25)
            return False

        inventory = device('package','list')
        was_active = target_state(inventory)
        output = ''
        try:
            if was_active:
                output += device('package','stop',identity)
                if not wait_active(False):
                    raise ToolError('Target did not stop; no package was uploaded')
            if digest(package) != package_digest:
                raise ToolError('Verified package changed; no package was uploaded')
            output += device('package','install',package,'--yes',
                             *(['--strict-compatibility'] if strict_compatibility else []),timeout=300)
            output += device('package','run',identity)
            if not wait_active(True):
                raise ToolError('Installation completed but the requested app did not become active; inspect device logs, Core and service compatibility')
        except Exception as error:
            # Never retry an uncertain installation. Restore only the exact
            # originally active identity, and do not claim old bytes were retained
            # when an install response could have been lost after commit.
            if was_active:
                try:
                    if not target_state(device('package','list')):
                        device('package','run',identity)
                        if not wait_active(True):
                            raise ToolError('Target activation was not confirmed')
                except Exception as restore_error:
                    raise ToolError(str(error)+'; target reactivation also failed: '+str(restore_error)) from error
            raise
        return {'app_id': metadata['id'], 'target': target, 'board': info.get('board'),
                'identity':identity,'package_sha256':package_digest,'stopped_running_target':was_active,
                'package': str(package), 'device': port, 'active_verified': True,
                'compatibility':compatibility, 'inventory_before': inventory, 'log': output}
