#!/usr/bin/env python3
"""Compare native audio SDK layout, stack and code with an earlier header.

This is a codec-only compiler probe, not a game FPS or device memory benchmark.
Both headers use the current shared SDK dependencies and the same O3 compiler.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import platform
import subprocess
import tempfile


PROBE = """#include <pxa/audio.hpp>
extern "C" pxa::Result<void> independent(pxa::AudioSession& s, std::span<const pxa::Tone> tones) {
    return s.effect(tones);
}
extern "C" pxa::Result<void> tracked(pxa::AudioSession& s, std::span<const pxa::Tone> tones, std::uint8_t track) {
#if TYPED_TRACKS
    return s.effect(tones, static_cast<pxa::EffectTrack>(track));
#else
    return s.effect(tones, track);
#endif
}
"""
LAYOUT = """#include <pxa/audio.hpp>
#include <cstdio>
int main() {
    std::printf("%zu %zu %zu\\n", sizeof(pxa::AudioSession), sizeof(pxa::Tone), sizeof(pxa::Result<void>));
}
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", default="HEAD")
    parser.add_argument("--cxx", default="clang++")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    system = Path(__file__).resolve().parents[2]
    sdk = system / "sdk/guest-cpp/include"
    relative = "sdk/guest-cpp/include/pxa/audio.hpp"
    baseline = subprocess.check_output(
        ["git", "show", f"{args.baseline}:{relative}"], cwd=system, text=True)
    revision = subprocess.check_output(
        ["git", "rev-parse", "--verify", f"{args.baseline}^{{commit}}"], cwd=system, text=True).strip()
    report = {
        "scope": __doc__, "baseline": revision, "machine": platform.machine(),
        "compiler": subprocess.check_output([args.cxx, "--version"], text=True).splitlines()[0],
    }
    with tempfile.TemporaryDirectory(prefix="pxa-audio-codec-") as name:
        root = Path(name)
        old = root / "baseline/pxa"
        old.mkdir(parents=True)
        (old / "audio.hpp").write_text(baseline)
        source = root / "codec.cpp"
        source.write_text(PROBE)
        layout = root / "layout.cpp"
        layout.write_text(LAYOUT)
        for label, header in (("before", baseline), ("after", (system / relative).read_text())):
            include = ["-I" + str(old.parent)] if label == "before" else []
            flags = [args.cxx, "-std=c++2c", "-O3", "-fno-exceptions", "-fno-rtti",
                     "-Wno-attributes", "-Wno-return-type-c-linkage", *include,
                     "-I" + str(sdk), "-I" + str(sdk / "pxa")]
            obj = root / f"{label}.o"
            typed = int("enum class EffectTrack" in header)
            subprocess.run([*flags, f"-DTYPED_TRACKS={typed}", "-fstack-usage",
                            "-c", str(source), "-o", str(obj)], check=True)
            executable = root / f"{label}-layout"
            subprocess.run([*flags, str(layout), "-o", str(executable)], check=True)
            sizes = map(int, subprocess.check_output([str(executable)], text=True).split())
            sections = {}
            for line in subprocess.check_output(["size", "-A", str(obj)], text=True).splitlines():
                fields = line.split()
                if fields and fields[0].startswith("."):
                    sections[fields[0]] = int(fields[1])
            stack = []
            for line in obj.with_suffix(".su").read_text().splitlines():
                symbol, size, kind = line.split("\t")
                stack.append({"function": symbol.split(":", 2)[-1],
                              "bytes": int(size), "kind": kind})
            undefined = subprocess.check_output(["nm", "-u", str(obj)], text=True)
            allocates = any(token in undefined for token in ("malloc", "calloc", "realloc", "_Znwm", "_Znam"))
            report[label] = {
                "layout_bytes": dict(zip(("AudioSession", "Tone", "Result<void>"), sizes)),
                "stack": stack, "text_bytes": sum(size for key, size in sections.items() if key.startswith(".text")),
                "static_storage_bytes": sum(size for key, size in sections.items() if key.startswith((".data", ".bss"))),
                "allocation_reference": allocates,
                "undefined_symbols": undefined.splitlines(),
            }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if report["before"]["layout_bytes"] != report["after"]["layout_bytes"]:
        raise SystemExit("Audio SDK object layout changed")
    if max(row["bytes"] for row in report["after"]["stack"]) > max(
            row["bytes"] for row in report["before"]["stack"]):
        raise SystemExit("Audio SDK compiler stack frame grew")
    for label in ("before", "after"):
        if report[label]["allocation_reference"] or report[label]["static_storage_bytes"]:
            raise SystemExit(f"{label} codec has allocation references or static storage")


if __name__ == "__main__":
    main()
