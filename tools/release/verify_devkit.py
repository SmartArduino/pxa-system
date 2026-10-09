#!/usr/bin/env python3
"""Exercise an installed DevKit outside its source checkout, without downloads."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--targets", default="simulator,esp32s3,esp32s31")
    args = parser.parse_args()
    kit = args.devkit.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PXA_USER_HOME=str(args.output.resolve() / "user"),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", PYTHONDONTWRITEBYTECODE="1",
               PXA_WASI_SDK_AUTO_DOWNLOAD="0", PXA_SIMULATOR_SOCKET_ROOT=str(args.output.resolve() / "sockets"))
    # Unix sockets have short path limits; isolate them outside deep worktrees.
    socket_directory = tempfile.TemporaryDirectory(prefix="pxa-kit-sockets-")
    env["PXA_SIMULATOR_SOCKET_ROOT"] = socket_directory.name
    entry = kit / "bin/pxa"
    results = []

    def execute(label, arguments, cwd=None, success=True):
        start = time.monotonic()
        result = subprocess.run([str(entry), *map(str, arguments)], cwd=cwd, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=600)
        (args.output / (label + ".log")).write_text(result.stdout)
        if (result.returncode == 0) != success:
            raise RuntimeError(f"{label}: unexpected status {result.returncode}: {result.stdout[-4000:]}")
        results.append({"name": label, "status": result.returncode, "seconds": time.monotonic() - start})
        return result.stdout

    execute("doctor", ["doctor", "--verify"])
    project_root = args.output.resolve() / "outside repository with spaces"
    project_root.mkdir(exist_ok=True)
    for language in ("c", "cpp"):
        name = "hello-" + language
        execute(name + "-init", ["init", name, "--language", language], project_root)
        app = project_root / name
        execute(name + "-build", ["build", "--target", args.targets], app)
        execute(name + "-check", ["check", app / "dist" / (name + ".pxa")], app)
        # A signed package must be rejected when one payload byte is changed.
        broken = app / "dist/broken"
        shutil.copytree(app / "dist" / ("pxa-" + name), broken)
        artifact = next((broken / "artifacts").glob("*.aot"))
        data = bytearray(artifact.read_bytes()); data[-1] ^= 1; artifact.write_bytes(data)
        execute(name + "-tamper", ["check", broken], app, False)
        shutil.rmtree(broken)
        original_lock = (app / "pxa.lock").read_bytes()
        lock = json.loads(original_lock); lock["distribution_sha256"] = "0" * 64
        (app / "pxa.lock").write_text(json.dumps(lock))
        execute(name + "-bad-lock", ["build"], app, False)
        (app / "pxa.lock").write_bytes(original_lock)
        # Actual native profile; incompatible service floor must fail before run.
        profile = json.loads(subprocess.check_output([kit / "bin/pxa-simulator", "--capabilities"], env=env))
        profile["services"] = [s | {"version": [0, 0]} if s["id"] == 3 else s for s in profile["services"]]
        profile_file = app / ".pxa/old-host.json"
        profile_file.parent.mkdir(exist_ok=True)
        profile_file.write_text(json.dumps(profile))
        execute(name + "-old-service", ["check", app / "dist" / (name + ".pxa"), "--host-profile", profile_file], app, False)
        baseline = (app / "package.json").read_bytes()
        execute(name + "-sdk-update", ["sdk", "update"], app)
        assert (app / "package.json").read_bytes() == baseline
        for iteration in range(2):
            log = (args.output / f"{name}-run-{iteration}.log").open("w")
            process = subprocess.Popen([entry, "run", "--profile", "generic", "--pxadb"],
                                       cwd=app, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                # The first run verifies the complete SDK/toolchain inventory.
                deadline = time.monotonic() + 180
                socket = None
                while time.monotonic() < deadline:
                    candidates = list(Path(socket_directory.name).glob("*.sock"))
                    if candidates and list(Path(socket_directory.name).glob("*.control")):
                        socket = candidates[0]; break
                    if process.poll() is not None:
                        raise RuntimeError(f"{name} simulator exited before display; inspect run log")
                    time.sleep(0.05)
                if socket is None: raise RuntimeError("simulator debug endpoint did not start")
                time.sleep(0.4)
                screenshot = args.output.resolve() / f"{name}-{iteration}.png"
                execute(f"{name}-capture-{iteration}", ["device", "screenshot", screenshot, "--port", "unix:" + str(socket)], app)
                if language == "c":
                    execute(f"{name}-tap-{iteration}", ["device", "input", "tap", "40", "60", "--port", "unix:" + str(socket)], app)
                else:
                    execute(f"{name}-tap-{iteration}", ["device", "input", "tap", "40", "124", "--port", "unix:" + str(socket)], app)
                    time.sleep(0.15)
                    after = args.output.resolve() / f"{name}-{iteration}-clicked.png"
                    execute(f"{name}-capture-clicked-{iteration}", ["device", "screenshot", after, "--port", "unix:" + str(socket)], app)
                    if hashlib.sha256(after.read_bytes()).digest() == hashlib.sha256(screenshot.read_bytes()).digest():
                        raise RuntimeError("C++ counter did not change after touch")
                execute(f"{name}-runtime-{iteration}", ["device", "runtime", "--port", "unix:" + str(socket)], app)
                execute(f"{name}-home-{iteration}", ["device", "input", "key", "home", "--port", "unix:" + str(socket)], app)
                process.wait(timeout=20)
                if process.returncode: raise RuntimeError(f"simulator shutdown status {process.returncode}")
                if language == "c" and "Button tapped" not in (args.output / f"{name}-run-{iteration}.log").read_text():
                    raise RuntimeError("C button input did not reach the Guest")
                results.append({"name": f"{name}-run-{iteration}", "status": 0,
                                "screenshot_sha256": hashlib.sha256(screenshot.read_bytes()).hexdigest()})
            finally:
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill(); process.wait()
                log.close()
    socket_directory.cleanup()
    distribution = json.loads((kit / "distribution.json").read_text())
    (args.output / "report.json").write_text(json.dumps({
        "release": distribution["release"], "source_commit": distribution["source_commit"],
        "distribution_sha256": hashlib.sha256((kit / "distribution.json").read_bytes()).hexdigest(),
        "targets": args.targets.split(","), "checks": results}, indent=2) + "\n")
    print(f"DevKit acceptance passed: {len(results)} checks; {args.output}")


if __name__ == "__main__":
    main()
