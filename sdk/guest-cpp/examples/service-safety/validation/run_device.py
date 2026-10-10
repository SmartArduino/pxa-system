"""Repeat service checks on an installed test package through PXADB.

This does not install packages, grant permissions, or change device settings.
Inspect the captures for Core/Audio checks OK, including after Home/resume.
"""

import argparse
import importlib.util
import json
from pathlib import Path
import time


def point(value):
    try:
        x, y = (int(part) for part in value.split(","))
        return x, y
    except ValueError as error:
        raise argparse.ArgumentTypeError("Expected x,y") from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pxadb", type=Path, required=True,
                        help="Path to the project's tools/pxadb/pxadb.py")
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--app", default="pxa-cpp-service-safety")
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--core-point", type=point, default=(70, 148))
    parser.add_argument("--audio-point", type=point, default=(70, 194))
    args = parser.parse_args()
    if args.repeat < 1:
        parser.error("--repeat must be positive")

    spec = importlib.util.spec_from_file_location("pxadb", args.pxadb.resolve())
    pxadb = importlib.util.module_from_spec(spec)
    # Dataclass annotations in pxadb need the module during import.
    import sys
    sys.modules[spec.name] = pxadb
    spec.loader.exec_module(pxadb)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"port": args.port, "app": args.app, "runs": []}

    with pxadb.PxaDbClient(args.port, 5) as client, \
            (output / "serial.jsonl").open("w", buffering=1) as log:
        def request(command):
            frames = client.request(command, timeout=20)
            log.write(json.dumps({
                "command": command,
                "frames": [{"kind": frame.kind, "payload": frame.payload}
                           for frame in frames],
            }) + "\n")
            return frames

        def pump(seconds):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                client.pump_logs()
                time.sleep(.02)

        def memory():
            result = {}
            for frame in request("MEMORY"):
                if frame.kind != "DATA":
                    continue
                properties = pxadb.info_properties(frame.payload)
                scope = properties.pop("scope")
                name = properties.pop("name", "")
                result[scope + (":" + name if name else "")] = {
                    key: int(value) for key, value in properties.items()
                }
            return result

        def tap(position):
            x, y = position
            request(f"INPUT POINTER DOWN {x} {y} 0")
            request("INPUT SYNC")
            pump(.08)
            request(f"INPUT POINTER UP {x} {y} 0")
            request("INPUT SYNC")

        def capture(name):
            for retry in range(3):
                try:
                    image = pxadb.screenshot_capture(request("SCREENSHOT"))
                    pxadb.write_screenshot(image, output / name)
                    return image.metadata
                except pxadb.PxaDbError as error:
                    if not pxadb.screenshot_retryable(error) or retry == 2:
                        raise

        report["hello"] = client.hello()
        if "heap-local" not in report["hello"]:
            raise RuntimeError("This firmware cannot measure local heap peaks")
        report["info"] = request("INFO")[-1].payload
        try:
            request("PACKAGE stop " + args.app)
        except pxadb.PxaDbRejectedError as error:
            if str(error) != "package_not_running":
                raise
        pump(.3)
        monitoring = running = False
        try:
            for index in range(args.repeat):
                row = {"index": index}
                report["runs"].append(row)
                request("MEMORY START")
                monitoring = True
                row["launch_start"] = memory()
                request("PACKAGE run " + args.app)
                running = True
                pump(1)
                row["core_image"] = capture(f"core-{index}.png")
                row["launch_end"] = memory()
                request("MEMORY STOP")
                monitoring = False
                request("MEMORY START")
                monitoring = True
                row["steady_start"] = memory()
                tap(args.core_point)
                pump(.5)
                tap(args.audio_point)
                pump(.5)
                row["audio_image"] = capture(f"audio-{index}.png")
                row["steady_end"] = memory()
                request("MEMORY STOP")
                monitoring = False
                for phase in ("launch", "steady"):
                    before = row[phase + "_start"]["heap"]
                    after = row[phase + "_end"]["heap"]
                    row[phase + "_heap"] = {
                        kind: {
                            "stable_delta": before[kind + "_free"] - after[kind + "_free"],
                            "true_peak_delta": before[kind + "_free"] - after[kind + "_min"],
                        }
                        for kind in ("sram", "psram")
                    }
                request("INPUT KEY HOME")
                pump(.3)
                request("PACKAGE run " + args.app)
                pump(.3)
                row["resume_image"] = capture(f"resume-{index}.png")
                request("PACKAGE stop " + args.app)
                running = False
                pump(.4)
                row["stopped"] = memory()
                (output / "device.json").write_text(json.dumps(report, indent=2) + "\n")
                print(json.dumps({"index": index, "launch": row["launch_heap"],
                                  "steady": row["steady_heap"]}), flush=True)
        finally:
            try:
                if monitoring:
                    request("MEMORY STOP")
            finally:
                try:
                    if running:
                        request("PACKAGE stop " + args.app)
                finally:
                    request("INPUT KEY HOME")


if __name__ == "__main__":
    main()
