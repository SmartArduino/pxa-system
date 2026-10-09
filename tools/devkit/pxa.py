#!/usr/bin/env python3
"""PXA App-only development entry point for an installed DevKit."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/release"))
import metadata
import compat


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".part")
    temp.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    os.replace(temp, path)


def distribution() -> dict:
    return load(ROOT / "distribution.json")


def inventory_matches(root: Path, item: dict, parents: dict | None = None) -> bool:
    relative = item["path"]
    if os.path.isabs(relative) or ".." in relative.split("/"):
        return False
    root_text = os.fspath(root)
    path = os.path.join(root_text, relative)
    # Resolve each shared directory once during this verification. This keeps
    # complete integrity checks cheap without caching trust across invocations.
    if parents is None:
        parents = {}
    parent = os.path.dirname(path)
    if parent not in parents:
        resolved = os.path.realpath(parent)
        parents[parent] = resolved == root_text or resolved.startswith(root_text + os.sep)
    if not parents[parent]:
        return False
    try:
        mode = os.lstat(path).st_mode
    except OSError:
        return False
    if "link" in item:
        return (stat.S_ISLNK(mode) and os.readlink(path) == item["link"] and
                os.path.realpath(path).startswith(root_text + os.sep))
    return stat.S_ISREG(mode) and metadata.digest(Path(path)) == item["sha256"]


def user_root() -> Path:
    base = os.environ.get("PXA_USER_HOME")
    if base:
        return Path(base).expanduser().resolve()
    return Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "pxa"


def tool(name: str) -> Path:
    paths = {
        "cmake": ROOT / "runtime/python-site/cmake/data/bin/cmake",
        "ninja": ROOT / "runtime/python-site/bin/ninja",
        "openssl": ROOT / "runtime/bin/openssl",
        "wamrc": ROOT / "runtime/bin/wamrc",
        "simulator": ROOT / "runtime/bin/pxsys_product_simulator",
        "desktop": ROOT / "runtime/bin/pxsys_desktop_simulator",
        "installer": ROOT / "runtime/bin/pxsys_package_installer",
    }
    path = paths[name]
    if not path.is_file():
        raise ValueError(f"DevKit tool is missing: {path}")
    return path


def environment() -> dict:
    env = dict(os.environ)
    env.update({
        "PYTHON": sys.executable, "PYTHONPATH": str(ROOT / "runtime/python-site"),
        "CMAKE": str(tool("cmake")), "WAMRC": str(ROOT / "bin/wamrc"),
        "WASI_SDK_DIR": str(ROOT / "toolchains/wasi-sdk"),
        "PXA_WASI_SDK_AUTO_DOWNLOAD": "0",
        "PXA_SIMULATOR_FONT": str(ROOT / "share/pxa/fonts/system.ttf"),
        "PXA_LZ4_LIBRARY": str(ROOT / "runtime/lib/liblz4.so.1"),
        "PATH": str(ROOT / "bin") + os.pathsep + env.get("PATH", ""),
    })
    return env


def stop_process(process, *, group=False):
    if process.poll() is not None:
        return
    def send(number):
        try:
            if group:
                os.killpg(process.pid, number)
            else:
                process.send_signal(number)
        except ProcessLookupError:
            pass
    send(signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        send(signal.SIGKILL)
        process.wait()


def execute(command: list, **kwargs):
    command = [str(x) for x in command]
    capture = kwargs.pop("capture_output", False)
    if capture:
        if "stdout" in kwargs or "stderr" in kwargs:
            raise ValueError("capture_output conflicts with stdout/stderr")
        kwargs.update(stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    timeout = kwargs.pop("timeout", None)
    input_data = kwargs.pop("input", None)
    with subprocess.Popen(command, env=environment(), start_new_session=True, **kwargs) as process:
        try:
            stdout, stderr = process.communicate(input_data, timeout=timeout)
        except BaseException:
            # A packaging shell can own Ninja/Clang children. Stop the entire
            # command group, and give a simulator time to save and shut down.
            stop_process(process, group=True)
            raise
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, command, stdout, stderr)
        return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)


def keys() -> tuple[Path, Path]:
    directory = user_root() / "keys"
    directory.mkdir(parents=True, mode=0o700, exist_ok=True)
    private = directory / "development.pem"
    public = directory / "development.der"
    external = os.environ.get("PXA_SIGNING_KEY")
    if external:
        private = Path(external).expanduser().resolve()
        if not private.is_file():
            raise ValueError("PXA_SIGNING_KEY must point to an existing private key")
        public = directory / (metadata.digest(private) + ".der")
    if not private.exists():
        # Install complete keys atomically; never ship an SDK-wide private key.
        with tempfile.TemporaryDirectory(dir=directory) as temp:
            candidate = Path(temp) / "private.pem"
            execute([ROOT / "bin/openssl", "ecparam", "-name", "prime256v1", "-genkey",
                     "-noout", "-out", candidate])
            candidate.chmod(0o600)
            # A concurrent process may already have created the key.
            try:
                os.link(candidate, private)
            except FileExistsError:
                pass
    if not public.exists():
        with tempfile.TemporaryDirectory(dir=directory) as temp:
            candidate = Path(temp) / "public.der"
            execute([ROOT / "bin/openssl", "pkey", "-in", private, "-pubout", "-outform", "DER", "-out", candidate])
            os.replace(candidate, public)
    return private, public


def lock_for(app: Path) -> dict:
    value = load(app / "pxa.lock")
    if value.get("schema") != "pxa-lock-1":
        raise ValueError("unsupported project lock schema")
    if value["release"] != metadata.release() or \
            value["distribution_sha256"] != metadata.digest(ROOT / "distribution.json"):
        raise ValueError(f"project requires DevKit {value['release']} with its original distribution; "
                         "use that DevKit or explicitly run pxa sdk update")
    if value["toolchain_sha256"] != distribution()["toolchain_sha256"]:
        raise ValueError("toolchain does not match project lock")
    parents = {}
    for item in distribution()["files"]:
        if not inventory_matches(ROOT, item, parents):
            raise ValueError("locked build input changed: " + item["path"])
    return value


def new_lock() -> dict:
    return {"schema": "pxa-lock-1", "release": metadata.release(),
            "distribution_sha256": metadata.digest(ROOT / "distribution.json"),
            "toolchain_sha256": distribution()["toolchain_sha256"],
            "compatibility": metadata.profile(), "optimization": "O3"}


def freeze_services(package: dict) -> dict:
    """Resolve shorthand once; later SDK updates preserve this declared baseline."""
    value = json.loads(json.dumps(package))
    versions = metadata.profile()["services"]
    value.setdefault("min_core", value.get("min_sdk", metadata.profile()["core"]))
    value.setdefault("target_core", value.get("target_sdk", value["min_core"]))
    value.setdefault("compile_core", value.get("compile_sdk", value["target_core"]))
    endpoints = {entry["component"] for entry in value.get("ipc_endpoints", [])}
    for component in value["components"]:
        required = {s["name"] if isinstance(s, dict) else s: s
                    for s in component.get("services", value.get("services", []))}
        if component.get("kind") == "ui":
            for name in ("window", "ui", "clock"):
                required.setdefault(name, name)
        if value.get("permissions"):
            required.setdefault("permission", "permission")
        if component["id"] in endpoints:
            required.setdefault("ipc", "ipc")
        if component.get("wasi"):
            required.setdefault("wasi", "wasi")
        component["services"] = [
            {"name": name, "min_version": versions[name]["version"],
             "max_version": [versions[name]["version"][0], 65535], "features": []}
            if isinstance(requirement, str) else requirement
            for name, requirement in sorted(required.items())]
    return value


def initialize(args) -> None:
    if not re.fullmatch(r"[a-z][a-z0-9._-]{0,59}", args.name):
        raise ValueError("App directory name must use lower-case ASCII letters, digits, ._- ")
    path = Path(args.name).resolve()
    if path.exists():
        raise ValueError("App directory already exists")
    template = ROOT / "sdk/templates" / args.language
    shutil.copytree(template, path)
    package = load(path / "package.json")
    package.update({"id": "pxa-" + args.name, "name": args.name, "version": "0.1.0",
                    "release_sequence": 1})
    write(path / "package.json", freeze_services(package))
    write(path / "pxa.lock", new_lock())
    (path / ".gitignore").write_text("dist/\n.pxa/\n")
    keys()
    print(f"Created {path}; SDK {metadata.release()}, O3, explicit service baseline")


def build(args) -> None:
    app = args.project.resolve()
    locked = lock_for(app)
    package = load(app / "package.json")
    private, public = keys()
    targets = args.target.split(",")
    if len(set(targets)) != len(targets) or any(t not in ("simulator", "esp32s3", "esp32s31") for t in targets):
        raise ValueError("target must be simulator, esp32s3, esp32s31 or a unique comma-separated list")
    destination = app / "dist"
    destination.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".build-", dir=destination) as temp:
        work = Path(temp)
        extras = work / "extra-aot"
        extras.mkdir()
        env = environment()
        env.update({"PXA_SIGNING_KEY": str(private),
                    "PXA_BUILD_CACHE_DIR": str(app / ".pxa/build-cache" / locked["distribution_sha256"][:16]),
                    "PXA_EXTRA_AOT_DIR": str(extras)})
        output = work / ("pxa-" + app.name)
        for target in targets:
            subprocess.run(["bash", str(ROOT / "tools/package/package_app.sh"),
                            str(app), target, str(output)], check=True, env=env)
            for artifact in (output / "artifacts").glob("*.aot"):
                shutil.copy2(artifact, extras / artifact.name)
        # Native install verification checks the signatures and all payloads.
        verify(output, public, work / "verify")
        provenance_path = Path(str(output) + ".pxa.provenance.json")
        provenance = load(provenance_path)
        provenance["devkit"] = {"release": metadata.release(),
                               "distribution_sha256": locked["distribution_sha256"],
                               "toolchain_sha256": locked["toolchain_sha256"],
                               "targets": targets}
        write(provenance_path, provenance)
        final_package = destination / ("pxa-" + app.name)
        # Rename after validation. Never recursively replace unrelated content.
        backup = work / "previous"
        if final_package.exists():
            if not (final_package / "manifest.pxm").is_file() or final_package.is_symlink():
                raise ValueError("refusing to replace an unrelated dist directory")
            final_package.rename(backup)
        try:
            output.rename(final_package)
        except BaseException:
            if backup.exists():
                backup.rename(final_package)
            raise
        for suffix in (".pxa", ".pxa.provenance.json"):
            os.replace(Path(str(output) + suffix), destination / (app.name + suffix))
    print(f"Built {destination / (app.name + '.pxa')}; targets={','.join(targets)}")


def verify(package: Path, public: Path, storage: Path) -> Path:
    storage.mkdir(parents=True, exist_ok=True)
    try:
        result = execute([ROOT / "bin/pxa-installer", "--storage-root", storage,
                          "--publisher-key", public, "--source", package,
                          "--expected-id", compat.identity(package)], capture_output=True, text=True)
    except subprocess.CalledProcessError as error:
        raise ValueError("native package verification failed: " +
                         (error.stderr or error.stdout or str(error)).strip()) from error
    # The installer prints the complete activated path, including publisher root.
    fields = dict(part.split("=", 1) for part in result.stdout.strip().split(";") if "=" in part)
    verified = Path(fields["root"]).resolve()
    if not verified.is_relative_to(storage.resolve()) or not (verified / "manifest.pxm").is_file():
        raise ValueError("installer did not produce a verified Package: " + result.stdout)
    return verified


def simulator_profile() -> dict:
    return json.loads(execute([ROOT / "bin/pxa-simulator", "--capabilities"],
                             capture_output=True, text=True).stdout)


def check_package(args) -> None:
    public = args.publisher_key.resolve() if args.publisher_key else keys()[1]
    if args.device_port:
        host = json.loads(execute([ROOT / "bin/pxadb", "runtime", "--port", args.device_port],
                                 capture_output=True, text=True).stdout)
    else:
        host = load(args.host_profile) if args.host_profile else simulator_profile()
    with tempfile.TemporaryDirectory(prefix="pxa-check-") as temp:
        verified = verify(args.package.resolve(), public, Path(temp))
        result = compat.analyze(compat.manifest_bytes(verified), host)
    print(json.dumps(result, indent=2))
    if not result["compatible"]:
        raise ValueError("Package is incompatible with this Host")


def run(args) -> None:
    app = args.project.resolve()
    lock_for(app)
    package = app / "dist" / ("pxa-" + app.name)
    _, public = keys()
    host = simulator_profile()
    with tempfile.TemporaryDirectory(prefix="pxa-run-check-") as temp:
        verified = verify(package, public, Path(temp))
        result = compat.analyze(compat.manifest_bytes(verified), host)
        if not result["compatible"]:
            raise ValueError("; ".join(result["errors"]))
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,60}", args.profile):
        raise ValueError("invalid screen profile")
    screen = load(ROOT / "share/pxa/profiles" / (args.profile + ".json"))
    # Debug service startup also creates this directory, but normal run must
    # work on a fresh project without depending on the optional PXADB process.
    state_root = app / ".pxa/state"
    state_root.mkdir(mode=0o700, parents=True, exist_ok=True)
    command = [ROOT / "bin/pxa-simulator", "--package", package,
               "--publisher-key", public, "--state-root", state_root]
    for name in ("width", "height", "density-dpi", "corner-radius", "locale"):
        if name in screen:
            command.extend(["--" + name, str(screen[name])])
    if "safe-insets" in screen:
        command.extend(["--safe-insets", ",".join(map(str, screen["safe-insets"]))])
    command.extend(args.simulator_args[1:] if args.simulator_args[:1] == ["--"] else args.simulator_args)
    if not args.pxadb:
        execute(command)
        return
    socket_root = Path(os.environ.get("PXA_SIMULATOR_SOCKET_ROOT", f"/tmp/pxa-simulator-{os.getuid()}"))
    socket_root.mkdir(mode=0o700, parents=True, exist_ok=True)
    if socket_root.stat().st_uid != os.getuid():
        raise ValueError("simulator socket directory is owned by another user")
    selector = args.profile + "@" + re.sub(r"[^a-z0-9-]", "-", app.name) + "-" + hashlib.sha256(str(app).encode()).hexdigest()[:8]
    socket_path = socket_root / (selector + ".sock")
    control_path = socket_root / (hashlib.sha256(selector.encode()).hexdigest()[:16] + ".control")
    if socket_path.exists() or control_path.exists():
        raise ValueError("simulator endpoint already exists; stop the existing instance first")
    profile_path = app / ".pxa/runtime-profile.json"
    write(profile_path, host)
    command.extend(["--pxadb-control-socket", control_path])
    service = subprocess.Popen([sys.executable, ROOT / "tools/devkit/simulator_pxadb.py",
        "--state-root", app / ".pxa/state", "--socket", socket_path,
        "--installer", ROOT / "bin/pxa-installer", "--publisher-key", public,
        "--control-socket", control_path, "--runtime-profile", profile_path], env=environment())
    try:
        deadline = time.monotonic() + 10
        while not socket_path.exists():
            if service.poll() is not None or time.monotonic() >= deadline:
                raise ValueError("PXADB simulator service failed to start")
            time.sleep(0.02)
        print(f"PXADB: --simulator {selector}", flush=True)
        execute(command)
    finally:
        stop_process(service)
        socket_path.unlink(missing_ok=True)
        control_path.unlink(missing_ok=True)


def doctor(args) -> None:
    errors = []
    for name in ("cmake", "ninja", "openssl", "wamrc", "simulator", "installer"):
        try:
            tool(name)
        except ValueError as error:
            errors.append(str(error))
    for path in (ROOT / "toolchains/wasi-sdk/bin/clang", ROOT / "share/pxa/fonts/system.ttf"):
        if not path.is_file():
            errors.append(f"missing {path}")
    if args.verify:
        parents = {}
        for item in distribution()["files"]:
            if not inventory_matches(ROOT, item, parents):
                errors.append("inventory mismatch: " + item["path"])
    for command in ([ROOT / "bin/cmake", "--version"], [ROOT / "bin/ninja", "--version"],
                    [ROOT / "bin/openssl", "version"], [ROOT / "bin/wamrc", "--version"],
                    [ROOT / "toolchains/wasi-sdk/bin/clang", "--version"]):
        try:
            execute(command, capture_output=True, text=True)
        except (ValueError, OSError, subprocess.CalledProcessError) as error:
            errors.append(str(error))
    try:
        host = simulator_profile()
        if host["pxa_release"] != metadata.release():
            errors.append("simulator release differs from SDK")
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        errors.append(str(error))
    report = {"release": metadata.release(), "host": distribution()["host_platform"],
              "errors": errors, "ready": not errors}
    print(json.dumps(report, indent=2))
    if errors:
        raise ValueError("DevKit is incomplete")


def sdk(args) -> None:
    if args.action == "install":
        import install
        install.install(args, user_root())
    elif args.action == "info":
        value = distribution()
        if not args.full:
            files = value.pop("files")
            value["inventory_entries"] = len(files)
            value["installed_file_bytes"] = sum(item.get("bytes", 0) for item in files)
            value["distribution_manifest"] = str(ROOT / "distribution.json")
        print(json.dumps(value, indent=2))
    elif args.action == "update":
        # Explicit update preserves the App's previously resolved service minima.
        lock_for_version = load(args.project / "pxa.lock")
        updated = new_lock()
        updated["compatibility"] = lock_for_version["compatibility"]
        write(args.project / "pxa.lock", updated)
        print(f"Pinned SDK {metadata.release()}; App service requirements preserved")


def device(args) -> None:
    arguments = args.arguments
    if arguments[:1] in (["install"], ["run"], ["stop"], ["uninstall"]):
        arguments = ["package", *arguments]
    execute([ROOT / "bin/pxadb", *arguments])


def main() -> int:
    # Convert termination into normal stack unwinding so child commands,
    # simulator services and atomic build directories are always cleaned up.
    signal.signal(signal.SIGTERM, lambda number, frame: sys.exit(128 + number))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version="PXA " + metadata.release())
    commands = parser.add_subparsers(dest="command", required=True)
    init = commands.add_parser("init")
    init.add_argument("name")
    init.add_argument("--language", choices=("c", "cpp"), default="cpp")
    init.set_defaults(handler=initialize)
    compile_app = commands.add_parser("build")
    compile_app.add_argument("--project", type=Path, default=Path.cwd())
    compile_app.add_argument("--target", default="simulator")
    compile_app.set_defaults(handler=build)
    inspect = commands.add_parser("check")
    inspect.add_argument("package", type=Path)
    inspect.add_argument("--host-profile", type=Path)
    inspect.add_argument("--device-port", help="query actual device capabilities (requires runtime-info firmware)")
    inspect.add_argument("--publisher-key", type=Path, help="explicit trusted publisher SPKI DER for another publisher")
    inspect.set_defaults(handler=check_package)
    simulate = commands.add_parser("run")
    simulate.add_argument("--project", type=Path, default=Path.cwd())
    simulate.add_argument("--sim", action="store_true")
    simulate.add_argument("--pxadb", action="store_true", help="enable optional screenshot/input/debug service")
    simulate.add_argument("--profile", default="generic")
    simulate.add_argument("simulator_args", nargs=argparse.REMAINDER)
    simulate.set_defaults(handler=run)
    diagnose = commands.add_parser("doctor")
    diagnose.add_argument("--verify", action="store_true", help="hash all installed distribution files")
    diagnose.set_defaults(handler=doctor)
    sdk_parser = commands.add_parser("sdk")
    sdk_parser.add_argument("action", choices=("info", "update", "install"))
    sdk_parser.add_argument("--project", type=Path, default=Path.cwd())
    sdk_parser.add_argument("--version")
    sdk_parser.add_argument("--archive", type=Path)
    sdk_parser.add_argument("--sha256")
    sdk_parser.add_argument("--destination", type=Path)
    sdk_parser.add_argument("--full", action="store_true", help="include the full SDK inventory in sdk info")
    sdk_parser.set_defaults(handler=sdk)
    deploy = commands.add_parser("device")
    deploy.add_argument("arguments", nargs=argparse.REMAINDER)
    deploy.set_defaults(handler=device)
    args = parser.parse_args()
    try:
        args.handler(args)
    except KeyboardInterrupt:
        return 130
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError) as error:
        print(f"pxa: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
