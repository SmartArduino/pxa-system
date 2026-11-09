from __future__ import annotations

import contextlib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


class ToolError(RuntimeError):
    pass


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


def identity(value: object) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def atomic_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(value, stream, sort_keys=True, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


@contextlib.contextmanager
def locked(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as stream:
        if os.name == 'nt':
            import msvcrt
            if not stream.tell():
                stream.write(b'\0')
                stream.flush()
            # LK_LOCK gives up after ten retries; LLVM builds can take hours.
            while True:
                stream.seek(0)
                try:
                    msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError as error:
                    if error.errno not in (13, 11, 36):
                        raise
                    time.sleep(0.1)
        else:
            import fcntl
            fcntl.flock(stream, fcntl.LOCK_EX)
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def executable(path: Path) -> Path:
    """Resolve native Windows executables without changing POSIX filenames."""
    return path.with_name(path.name + '.exe') if os.name == 'nt' and path.suffix != '.exe' else path


def process_options() -> dict:
    return ({'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt'
            else {'start_new_session': True})


def command(args, *, env=None, capture=False, cwd=None, strip=True):
    args = [str(value) for value in args]
    result = subprocess.run(args, env=env, cwd=cwd, text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    if result.returncode:
        raise ToolError(f'{Path(args[0]).name} failed ({result.returncode})' +
                        (': ' + result.stderr.strip() if capture else ''))
    return (result.stdout.strip() if strip else result.stdout) if capture else ''
