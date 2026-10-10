"""Run an identical counter against two SDK builds using the actual product Host."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--simulator-build", type=Path, required=True)
parser.add_argument("--before-package", type=Path, required=True)
parser.add_argument("--after-package", type=Path, required=True)
parser.add_argument("--publisher-key", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
build = args.simulator_build.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
sdk = source.parents[5]
target = build / "CMakeFiles/pxsys_declarative_ui_test.dir"
flags = {}
for line in (target / "flags.make").read_text().splitlines():
    if " = " in line:
        name, value = line.split(" = ", 1)
        flags[name] = shlex.split(value)
command = shlex.split((target / "link.txt").read_text())
obj = output / "counter_aot_probe.o"
binary = output / "counter_aot_probe"
subprocess.run([command[0], *flags["C_DEFINES"], *flags["C_INCLUDES"], *flags["C_FLAGS"],
    '-DPXA_PRODUCT_RUNNER="' + str(sdk / "simulator/desktop/product_runner.c") + '"',
    "-c", str(source / "counter_aot_probe.c"), "-o", str(obj)], check=True)
command = [str(obj) if part.endswith("declarative_ui_test.c.o") else part
           for part in command if not part.startswith("-Wl,--dependency-file=")]
command[command.index("-o") + 1] = str(binary)
subprocess.run(command, cwd=build, check=True)
result = {"before": [], "after": []}
for repeat in range(3):
    order = ("before", "after") if repeat % 2 == 0 else ("after", "before")
    for side in order:
        state = output / f"state-{side}-{repeat}"
        state.mkdir(exist_ok=True)
        run = subprocess.run([str(binary), str(getattr(args, side + "_package").resolve()),
                              str(args.publisher_key.resolve()), str(state)],
                             check=True, text=True, capture_output=True)
        (output / f"{side}-{repeat}.log").write_text(run.stdout + run.stderr)
        result[side].append(json.loads(next(line.removeprefix("COUNTER_MEMORY ")
            for line in run.stdout.splitlines() if line.startswith("COUNTER_MEMORY "))))
assert all(value == result["before"][0] for values in result.values() for value in values), result
(output / "counter-memory.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result["after"][0]))
