#!/usr/bin/env python3
"""Compare IME snapshots with the same configured Linux desktop/Ninja build.

Build pxsys_ime_test first. Timing runs exclude allocation tracing; --memory
counts requested bytes in statically linked code, including LVGL draw threads.
Neither mode measures a physical LCD, input sampling, or Guest execution.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import statistics
import subprocess


WORKLOADS = ("candidate_reply", "empty_reply", "caret_move", "full_redraw", "arrow_press")


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--baseline-source", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--iterations", type=positive, default=32)
    parser.add_argument("--samples", type=positive, default=5)
    parser.add_argument("--memory", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    sdk = Path(__file__).resolve().parents[2]
    output = (args.output_dir or build / "ime-render-benchmark").resolve()
    output.mkdir(parents=True, exist_ok=True)

    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "pxsys_ime_test"], text=True
    ).splitlines()
    recipe = next(line for line in commands if " -c " in line and "pxa_lvgl_ime.c.o" in line)
    compile_args = shlex.split(recipe)
    compile_args = compile_args[:compile_args.index("-MD")]
    compile_args += ["-I" + str(sdk / "libpxa/adapters/lvgl")]
    link_args = shlex.split(commands[-1])
    start = link_args.index("&&") + 1 if link_args[0] == ":" else 0
    end = link_args.index("&&", start) if "&&" in link_args[start:] else len(link_args)
    link_args = [part for part in link_args[start:end] if not part.endswith(".o")]
    if args.memory:
        compile_args.append("-DIME_BENCHMARK_MEMORY")
        link_args.append("-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free")

    sources = {"after": sdk / "libpxa/adapters/lvgl/pxa_lvgl_ime.c"}
    if args.baseline_source:
        sources = {"before": args.baseline_source.resolve(), **sources}
    for name, source in sources.items():
        obj = output / (name + ".o")
        executable = output / name
        subprocess.run(
            compile_args + ['-DIME_SOURCE="' + str(source) + '"', "-o", str(obj), "-c",
                            str(Path(__file__).with_name("ime_render_benchmark.c"))],
            cwd=build, check=True,
        )
        link = link_args.copy()
        link[link.index("-o") + 1] = str(executable)
        link.insert(1, str(obj))
        subprocess.run(link, cwd=build, check=True)

    result = {
        "iterations": args.iterations,
        "allocation_tracing": args.memory,
        "sources": {name: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                    for name, path in sources.items()},
        "samples": [],
    }
    for circle in (0, 1):
        for workload in range(len(WORKLOADS)):
            for sample in range(args.samples):
                order = list(sources) if sample % 2 == 0 else list(reversed(sources))
                for name in order:
                    raw = subprocess.check_output(
                        [str(output / name), str(circle), str(workload), str(args.iterations)], text=True
                    )
                    value = json.loads(raw.splitlines()[-1])
                    result["samples"].append({"variant": name, **value})
            (output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
            for name in sources:
                samples = [value for value in result["samples"] if value["variant"] == name
                           and value["circle"] == circle and value["workload"] == workload]
                summary = {key: statistics.median(value[key] for value in samples)
                           for key in samples[0] if key not in ("variant", "circle", "workload")}
                print(json.dumps({"variant": name, "circle": circle,
                                  "workload": WORKLOADS[workload], **summary}), flush=True)


if __name__ == "__main__":
    main()
