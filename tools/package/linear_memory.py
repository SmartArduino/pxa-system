# SPDX-License-Identifier: MIT
"""Resolve the signed linear-memory policy shared by a package's build targets."""

import json
from pathlib import Path
from typing import NamedTuple


class LinearMemory(NamedTuple):
    initial: int
    maximum: int
    pinned: bool


def _target_recipes():
    path = Path(__file__).resolve().parents[2] / "config/aot-targets.json"
    return json.loads(path.read_text(encoding="utf-8"))


def targets_for_artifacts(aot_targets):
    if not aot_targets:
        return ["wasm"]
    recipes = _target_recipes()
    names = {recipe["target"]: name for name, recipe in recipes.items() if name != "wasm"}
    return [names.get(target, target) for target in aot_targets]


def memory_for_targets(memory, targets=()):
    if memory is None:
        return LinearMemory(0, 0, False)
    fields = ("initial_bytes", "maximum_bytes", "pinned")
    allowed = set(fields) | {field + "_by_target" for field in fields}
    if (not isinstance(memory, dict) or memory.keys() - allowed or
            not {"maximum_bytes", "pinned"} <= memory.keys()):
        raise ValueError("linear_memory requires maximum_bytes and boolean pinned")

    overrides = {}
    known_targets = _target_recipes() if any(field + "_by_target" in memory for field in fields) else {}
    for field in fields:
        key = field + "_by_target"
        mapping = memory.get(key, {})
        if not isinstance(mapping, dict):
            raise ValueError(f"{key} must be an object")
        if mapping.keys() - known_targets:
            raise ValueError(f"unknown linear memory target in {key}")
        for value in ([memory[field]] if field in memory else []) + list(mapping.values()):
            if field == "pinned":
                if type(value) is not bool:
                    raise ValueError(f"{field} must be a boolean")
            elif type(value) is not int or not 65536 <= value <= 4294967296 or value % 65536:
                raise ValueError(f"{field} must be a page-aligned WebAssembly size")
        overrides[field] = mapping

    def policy(target):
        initial = overrides["initial_bytes"].get(target, memory.get("initial_bytes", 0))
        maximum = overrides["maximum_bytes"].get(target, memory["maximum_bytes"])
        pinned = overrides["pinned"].get(target, memory["pinned"])
        if initial > maximum:
            raise ValueError(f"initial linear memory exceeds maximum for {target or 'default'}")
        return LinearMemory(initial, maximum, pinned)

    default = policy(None)
    configured = {target: policy(target) for target in set().union(*overrides.values())}
    selected = [configured.get(target, default) for target in targets] or [default]
    initial = max(p.initial for p in selected)
    maximum = min(p.maximum for p in selected)
    if initial > maximum:
        raise ValueError("the selected targets have incompatible linear memory limits")
    return LinearMemory(initial, maximum, any(p.pinned for p in selected))


def maximum_for_targets(memory, targets=()):
    return memory_for_targets(memory, targets).maximum
