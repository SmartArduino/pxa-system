#!/usr/bin/env python3
"""Verify complete text from actual system-keyboard edits on pai-touch.

Install the sibling dynamic-input test package and unlock the screen first.
Only the dedicated QA application is restarted. Requires workspace PXADB.
"""
import argparse
import json
import sys
import time
from pathlib import Path

root = next(p for p in Path(__file__).resolve().parents if (p / "tools/pxadb/pxadb.py").is_file())
sys.path.insert(0, str(root / "tools/pxadb"))
import pxadb as db

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", default="/dev/ttyACM0")
parser.add_argument("--output", type=Path, required=True)
a = parser.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
identity = "pxa-cpp-input-qa"
end = time.monotonic() + 55
while True:
    try:
        client = db.open_client(a.port, 15)
        break
    except OSError as error:
        if "exclusively lock" not in str(error) or time.monotonic() >= end:
            raise
        time.sleep(.2)
log = (a.output / "host.log").open("w")
lines = []
def record(text):
    lines.append(text)
    log.write(text + "\n")
    log.flush()
client.log_callback = lambda frame: record(frame.payload)
client.raw_callback = lambda raw: record(raw.decode(errors="replace"))
monitor = False
results = {}
def request(command):
    return client.request(command)
def memory(command="MEMORY"):
    return {row["scope"]: {k: int(v) for k, v in row.items() if k not in ("scope", "name")}
            for frame in request(command) if frame.kind == "DATA"
            for row in [db.info_properties(frame.payload)]}
def active():
    running = [frame.payload.split("\t")[0] for frame in request("PACKAGES")
               if frame.kind == "PKG" and "active=1" in frame.payload]
    assert len(running) == 1 and running[0].endswith(":" + identity), running
    assert not any("task_wdt" in line for line in lines), "Text layout triggered watchdog"
def tap(x, y):
    active()
    request(f"INPUT TAP {x} {y}")
    time.sleep(1.5)
def capture(name):
    db.write_screenshot(db.screenshot_capture(request("SCREENSHOT JPEG AFTER_PRESENT")),
                        a.output / (name + ".jpg"))
def stop():
    try:
        request("PACKAGE stop " + identity)
    except db.PxaDbError as error:
        if "package_not_running" not in str(error):
            raise
try:
    client.subscribe_logs()
    stop()
    results["idle"] = memory()
    request("MEMORY START"); monitor = True
    request("PACKAGE run " + identity)
    time.sleep(3)
    active()
    results["initialization"] = memory("MEMORY STOP"); monitor = False
    capture("initial")
    request("MEMORY START"); monitor = True
    for sample, size in enumerate((499, 2047, 4051, 0)):
        before = time.monotonic()
        first_line = len(lines)
        if sample:
            tap(109, 95)
        tap(40, 95)
        tap(262, 145)  # Backspace deletes one ASCII character at the end.
        tap(262, 192)  # The system IME closes and submits.
        marker = f"DYNAMIC sample={sample} bytes={size} "
        until = time.monotonic() + 3
        while not any(marker in line and "PASS" in line for line in lines[first_line:]):
            assert not any("DYNAMIC" in line and "FAIL" in line for line in lines[first_line:]), "Incorrect full-text value"
            assert time.monotonic() < until, "Missing full-text submission: " + marker
            request("MEMORY")  # Also pumps the asynchronous log frames.
            time.sleep(.1)
        results[f"sample_{sample}"] = memory()
        results[f"sample_{sample}"]["test_elapsed_ms"] = round((time.monotonic() - before) * 1000)
    results["edits"] = memory("MEMORY STOP"); monitor = False
    capture("submitted-empty")
    for repeat in range(3):
        stop(); time.sleep(.5)
        request("PACKAGE run " + identity); time.sleep(2)
        active()
    stop(); time.sleep(.8)
    results["after_stop"] = memory()
    (a.output / "memory.json").write_text(json.dumps(results, indent=2) + "\n")
    print("Device: long Chinese/URL/maximum/empty edits and three repeated launches PASS")
finally:
    if monitor:
        request("MEMORY STOP")
    stop()
    client.close()
    log.close()
