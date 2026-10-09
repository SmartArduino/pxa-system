#!/usr/bin/env python3
"""Compare the same serialized service workload using two SDK checkouts."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-sdk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--iterations", type=int, default=2_000_000)
    parser.add_argument("--samples", type=int, default=12)
    parser.add_argument("--cpu", type=int)
    args = parser.parse_args()
    if args.iterations < 1 or args.iterations > 100_000_000 or args.samples < 1:
        parser.error("iterations must be 1..100000000 and samples must be positive")
    if hasattr(os, "sched_getaffinity"):
        allowed = os.sched_getaffinity(0)
        cpu = args.cpu if args.cpu is not None else max(allowed)
        if cpu not in allowed:
            parser.error("CPU is outside this process's allowed affinity")
        os.sched_setaffinity(0, {cpu})
    else:
        cpu = None
    sdk = Path(__file__).resolve().parents[1]
    flags = ["-std=c++2c", "-O3", "-fno-exceptions", "-fno-rtti", "-Wno-attributes"]
    samples = {op: {version: [] for version in ("before", "after")}
               for op in ("storage", "permission")}
    pools = {}
    with tempfile.TemporaryDirectory(prefix="pxa-service-bench-") as directory:
        binaries = {}
        for version, headers in (("before", args.reference_sdk.resolve()), ("after", sdk)):
            binary = Path(directory) / version
            subprocess.run([args.compiler, *flags, "-I" + str(headers / "include"),
                            str(sdk / "tests/service_bench.cpp"), str(headers / "src/runtime.cpp"),
                            "-o", str(binary)], check=True)
            binaries[version] = binary
        for index in range(args.samples):
            for mode, op in enumerate(samples):
                order = ("before", "after") if index % 2 == 0 else ("after", "before")
                for version in order:
                    result = json.loads(subprocess.check_output(
                        [str(binaries[version]), str(mode), str(args.iterations)], text=True))
                    samples[op][version].append(result["ns_per_request"])
                    pools[version] = {key: result[key] for key in ("peak_slots", "pool_reserved_bytes")}
    report = {
        "scope": "Native mock request encoding/task dispatch/completion; excludes Host, IO, queue/display and device FPS",
        "compiler": subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0],
        "flags": flags, "cpu": cpu, "iterations_per_sample": args.iterations,
        "samples_per_workload": args.samples, "pools": pools,
        "timings": {op: {version: {"median_ns": statistics.median(values),
                    "min_ns": min(values), "max_ns": max(values), "samples_ns": values}
                    for version, values in versions.items()}
                    for op, versions in samples.items()},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
