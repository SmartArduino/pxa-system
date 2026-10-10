"""Alternate the same native workload against two guest-cpp SDK directories."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--before", type=Path, required=True)
parser.add_argument("--after", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
result = {"before": [], "after": []}
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    for side in result:
        sdk = getattr(args, side).resolve()
        subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++2c", "-O3",
            "-fno-exceptions", "-fno-rtti", "-Wno-attributes", "-I", str(sdk / "include"),
            str(Path(__file__).with_name("counter_probe.cpp")), str(sdk / "src/runtime.cpp"),
            "-o", str(directory / side)], check=True)
    for repeat in range(8):
        order = ["before", "after"] if repeat % 2 == 0 else ["after", "before"]
        for side in order:
            result[side].append(json.loads(subprocess.check_output([str(directory / side)])))
for name in ("page_bytes", "state_bytes", "context_bytes", "scope_bytes", "pool_bytes", "imports", "digest", "idle_imports"):
    expected = result["before"][0][name]
    assert all(run[name] == expected for runs in result.values() for run in runs), name
args.output.write_text(json.dumps(result, indent=2) + "\n")
