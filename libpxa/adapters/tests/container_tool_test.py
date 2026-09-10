#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


PXA_SYSTEM_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PXA_SYSTEM_ROOT / "tools/wamr"))
from metadata import load as load_wamr_metadata


def run(*args: object, ok: bool = True) -> subprocess.CompletedProcess:
    result = subprocess.run([str(arg) for arg in args], check=False,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (result.returncode == 0) != ok:
        raise RuntimeError(
            f"unexpected exit {result.returncode}: {' '.join(map(str, args))}\n"
            f"{result.stdout.decode(errors='replace')}"
            f"{result.stderr.decode(errors='replace')}")
    return result


def metadata(path: Path, sequence: int, lineage: str | None) -> None:
    value = {
        "format": "pxa-package-source-0.1",
        "id": "pxa-rotation-test",
        "version": f"{sequence}.0.0",
        "release_sequence": sequence,
    }
    if lineage is not None:
        value["publisher_lineage"] = lineage
    path.write_text(json.dumps(value), encoding="utf-8")


def signed_package(root: Path, tools: Path, key: Path, meta: Path,
                   output: Path) -> Path:
    package = output.with_suffix("")
    artifacts = package / "artifacts"
    artifacts.mkdir(parents=True)
    (artifacts / "main.wasm").write_bytes(b"wasm" * 300)
    (artifacts / "main.linux-x86_64.aot").write_bytes(b"aot" * 500)
    run(sys.executable, tools / "build_package_manifest.py", meta, package,
        key, "linux-x86_64", load_wamr_metadata()["engine_abi"])
    run(sys.executable, tools / "build_pxa_container.py", package, key, output)
    return output


def main() -> None:
    verifier = Path(sys.argv[1]).resolve()
    tools = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="pxa-lineage-test-") as temporary:
        root = Path(temporary)
        old_key = root / "old.pem"
        new_key = root / "new.pem"
        old_public = root / "old.der"
        run("openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
            "ec_paramgen_curve:P-256", "-out", old_key)
        run("openssl", "genpkey", "-algorithm", "EC", "-pkeyopt",
            "ec_paramgen_curve:P-256", "-out", new_key)
        run("openssl", "pkey", "-in", old_key, "-pubout", "-outform", "DER",
            "-out", old_public)
        lineage = root / "publisher-lineage.pxkl"
        run(sys.executable, tools / "build_publisher_lineage.py",
            "pxa-rotation-test", old_key, new_key, lineage)

        rotated_meta = root / "rotated.json"
        metadata(rotated_meta, 2, lineage.name)
        rotated = signed_package(root, tools, new_key, rotated_meta,
                                 root / "rotated.pxa")
        storage_ok = root / "storage-ok"
        storage_ok.mkdir()
        run(verifier, old_public, storage_ok, rotated)

        tampered = root / "tampered.pxa"
        tampered.write_bytes(rotated.read_bytes()[:-1] +
                             bytes([rotated.read_bytes()[-1] ^ 1]))
        storage_tampered = root / "storage-tampered"
        storage_tampered.mkdir()
        run(verifier, old_public, storage_tampered, tampered, ok=False)

        old_meta = root / "old.json"
        metadata(old_meta, 1, None)
        old = signed_package(root, tools, old_key, old_meta, root / "old.pxa")
        storage_rollback = root / "storage-rollback"
        storage_rollback.mkdir()
        run(verifier, old_public, storage_rollback, rotated, old, ok=False)

        rollback_lineage = root / "publisher-lineage-rollback.pxkl"
        run(sys.executable, tools / "build_publisher_lineage.py",
            "pxa-rotation-test", old_key, new_key, rollback_lineage,
            "--keep-old-signer")
        rollback_meta = root / "rollback-allowed.json"
        metadata(rollback_meta, 2, rollback_lineage.name)
        rollback_allowed = signed_package(
            root, tools, new_key, rollback_meta, root / "rollback-allowed.pxa")
        storage_allowed = root / "storage-rollback-allowed"
        storage_allowed.mkdir()
        run(verifier, old_public, storage_allowed, rollback_allowed, old)


if __name__ == "__main__":
    main()
