#!/usr/bin/env python3
"""Apply the ordered PXA patch series to an out-of-tree WAMR source overlay."""

from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
import re
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_METADATA = ROOT / "config/wamr.json"
DEFAULT_SERIES = ROOT / "platforms/esp-idf/wamr/patches/series"
HUNK_PATTERN = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")


@dataclasses.dataclass
class Hunk:
    old_start: int
    old_count: int
    new_start: int
    new_count: int
    lines: list[str]


@dataclasses.dataclass
class FilePatch:
    path: pathlib.PurePosixPath
    hunks: list[Hunk]


class PatchError(RuntimeError):
    pass


def git(source: pathlib.Path, *arguments: str) -> str:
    process = subprocess.run(
        ["git", "-C", str(source), *arguments], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if process.returncode != 0:
        raise PatchError(process.stderr.strip() or "git command failed")
    return process.stdout.strip()


def parse_patch(path: pathlib.Path) -> list[FilePatch]:
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    files: list[FilePatch] = []
    current: FilePatch | None = None
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("+++ b/"):
            relative = pathlib.PurePosixPath(line[6:].rstrip("\n"))
            if relative.is_absolute() or ".." in relative.parts:
                raise PatchError(f"unsafe path in {path.name}: {relative}")
            current = FilePatch(relative, [])
            files.append(current)
            index += 1
            continue
        match = HUNK_PATTERN.match(line)
        if match:
            if current is None:
                raise PatchError(f"hunk without file header in {path.name}")
            hunk = Hunk(
                int(match.group(1)), int(match.group(2) or 1),
                int(match.group(3)), int(match.group(4) or 1), [],
            )
            index += 1
            while index < len(lines):
                body = lines[index]
                if body.startswith(("diff --git ", "@@ ", "--- ", "+++ ")):
                    break
                if body.startswith("\\ No newline at end of file"):
                    index += 1
                    continue
                if not body.startswith((" ", "+", "-")):
                    raise PatchError(f"unsupported patch line in {path.name}: {body.rstrip()}")
                hunk.lines.append(body)
                index += 1
            current.hunks.append(hunk)
            continue
        index += 1
    if not files or any(not item.hunks for item in files):
        raise PatchError(f"no complete file patches found in {path.name}")
    return files


def apply_hunks(original: list[str], hunks: list[Hunk], label: str) -> list[str]:
    output: list[str] = []
    source_index = 0
    for hunk in hunks:
        target_index = hunk.old_start - 1
        if target_index < source_index or target_index > len(original):
            raise PatchError(f"invalid hunk position in {label}")
        output.extend(original[source_index:target_index])
        source_index = target_index
        old_seen = 0
        new_seen = 0
        for line in hunk.lines:
            marker, content = line[0], line[1:]
            if marker in (" ", "-"):
                if source_index >= len(original) or original[source_index] != content:
                    actual = "<eof>" if source_index >= len(original) else original[source_index].rstrip()
                    raise PatchError(
                        f"patch context mismatch in {label} at source line "
                        f"{source_index + 1}: found {actual!r}"
                    )
                source_index += 1
                old_seen += 1
            if marker in (" ", "+"):
                output.append(content)
                new_seen += 1
        if old_seen != hunk.old_count or new_seen != hunk.new_count:
            raise PatchError(f"hunk count mismatch in {label}")
    output.extend(original[source_index:])
    return output


def prepare(source: pathlib.Path, output: pathlib.Path,
            metadata_path: pathlib.Path, series_path: pathlib.Path) -> list[pathlib.Path]:
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    expected_commit = metadata["commit"]
    actual_commit = git(source, "rev-parse", "HEAD")
    if actual_commit != expected_commit:
        raise PatchError(f"WAMR is {actual_commit}; expected {expected_commit}")
    initial_status = git(source, "status", "--porcelain", "--untracked-files=no")
    if initial_status:
        raise PatchError("WAMR submodule has tracked local changes")

    patch_names = [
        line.strip() for line in series_path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if not patch_names:
        raise PatchError("WAMR patch series is empty")
    patch_dir = series_path.parent
    staged: dict[pathlib.PurePosixPath, list[str]] = {}
    for patch_name in patch_names:
        patch_path = patch_dir / patch_name
        if not patch_path.is_file():
            raise PatchError(f"missing patch in series: {patch_name}")
        for file_patch in parse_patch(patch_path):
            source_path = source.joinpath(*file_patch.path.parts)
            if file_patch.path not in staged:
                if not source_path.is_file():
                    raise PatchError(f"patched WAMR source is missing: {file_patch.path}")
                staged[file_patch.path] = source_path.read_text(encoding="utf-8").splitlines(keepends=True)
            staged[file_patch.path] = apply_hunks(
                staged[file_patch.path], file_patch.hunks,
                f"{patch_name}:{file_patch.path}",
            )

    if output.exists():
        shutil.rmtree(output)
    written: list[pathlib.Path] = []
    for relative, content in staged.items():
        destination = output.joinpath(*relative.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text("".join(content), encoding="utf-8")
        written.append(destination)
    if git(source, "status", "--porcelain", "--untracked-files=no") != initial_status:
        raise PatchError("WAMR submodule changed while preparing the overlay")
    return written


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--metadata", type=pathlib.Path, default=DEFAULT_METADATA)
    parser.add_argument("--series", type=pathlib.Path, default=DEFAULT_SERIES)
    arguments = parser.parse_args()
    try:
        written = prepare(arguments.source.resolve(), arguments.output.resolve(),
                          arguments.metadata.resolve(), arguments.series.resolve())
    except (OSError, KeyError, json.JSONDecodeError, PatchError) as error:
        print(f"PXA WAMR patch error: {error}", file=sys.stderr)
        return 1
    print(f"Prepared WAMR overlay at {arguments.output.resolve()} ({len(written)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
