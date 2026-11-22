#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the experimental memory-access optimizer without changing installed tools.

Uses PXA's pinned WAMR checkout and existing pinned LLVM build. Source,
build logs and provenance belong in a fresh ignored artifact directory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import subprocess

SDK = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=SDK / "wamr")
    parser.add_argument("--llvm", type=Path, help="Directory containing pinned LLVMConfig.cmake")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--initial-bound-ranges", action="store_true",
                        help="Also expose guaranteed initial-memory bounds to LLVM (workload-dependent performance)")
    args = parser.parse_args()
    if platform.machine().lower() not in ("x86_64", "amd64"):
        parser.error("This experiment currently builds an x86_64 host compiler")
    metadata = json.loads((SDK / "config/wamr.json").read_text())
    revision = subprocess.check_output(["git", "-C", str(args.source), "rev-parse", "HEAD"], text=True).strip()
    if revision != metadata["commit"]:
        parser.error("WAMR source does not match config/wamr.json")
    llvm = args.llvm or SDK / (".pxa/llvm-" + metadata["llvm"]["commit"]) / "build/lib/cmake/llvm"
    if not (llvm / "LLVMConfig.cmake").is_file():
        parser.error("Build the pinned LLVM first, or supply --llvm")
    llvm_revision = subprocess.check_output(["git", "-C", str(llvm), "rev-parse", "HEAD"], text=True).strip()
    if llvm_revision != metadata["llvm"]["commit"]:
        parser.error("LLVM checkout does not match config/wamr.json")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = output / "wamr"
    if source.exists():
        parser.error("Use a fresh output directory")
    patch = Path(__file__).resolve().parent / "patches/linear-memory-access.patch"
    commands = []

    def run(command):
        commands.append([str(value) for value in command])
        subprocess.run(command, check=True)

    run(["git", "clone", "--quiet", "--shared", str(args.source.resolve()), str(source)])
    run(["git", "-C", str(source), "checkout", "--quiet", "--detach", revision])
    run(["git", "-C", str(source), "apply", "--check", str(patch)])
    run(["git", "-C", str(source), "apply", str(patch)])
    range_patch = patch.with_name("initial-memory-ranges.patch")
    if args.initial_bound_ranges:
        run(["git", "-C", str(source), "apply", "--check", str(range_patch)])
        run(["git", "-C", str(source), "apply", str(range_patch)])
    build = output / "build"
    run(["cmake", "-S", str(source / "wamr-compiler"), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DWAMR_BUILD_WITH_CUSTOM_LLVM=1",
         "-DLLVM_DIR=" + str(llvm.resolve()), "-DWAMR_BUILD_TARGET=X86_64"])
    run(["cmake", "--build", str(build), "--parallel", str(args.jobs)])
    compiler = build / "wamrc"
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {"source": metadata["repository"], "source_commit": revision,
                "llvm_commit": metadata["llvm"]["commit"], "patch_sha256": sha(patch),
                "compiler_sha256": sha(compiler), "compiler": str(compiler),
                "engine_abi_unchanged": metadata["engine_abi"], "commands": commands,
                "installed_tools_modified": False}
    manifest["initial_bound_ranges"] = args.initial_bound_ranges
    if args.initial_bound_ranges:
        manifest["bound_ranges_patch_sha256"] = sha(range_patch)
    (output / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(compiler)


if __name__ == "__main__":
    main()
