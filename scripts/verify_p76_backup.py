#!/usr/bin/env python3
"""Assemble and independently verify a QEAFRESCUE p76/boot four-part backup."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
import re
import sys

SIZE = 64 * 1024 * 1024
CHUNK = 16 * 1024 * 1024
NAMES = tuple(f"P760000{i}.BIN" for i in range(1, 5))


class BackupError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def read_info(folder: Path) -> str:
    path = folder / "P76INFO.TXT"
    if not path.is_file():
        raise BackupError(f"missing {path}")
    text = path.read_text(encoding="utf-8", errors="strict")
    fields = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    required = {
        "state": "COMPLETE",
        "device": "violet",
        "partition": "boot (p76)",
        "source": "/dev/mmcblk0p76",
        "size": str(SIZE),
        "chunk_size": str(CHUNK),
        "chunk_count": "4",
        "completed_chunks": "4",
        "read_mode": "O_RDONLY",
    }
    for key, value in required.items():
        if fields.get(key) != value:
            raise BackupError(f"{folder}: {key} expected {value!r}, got {fields.get(key)!r}")
    return text


def inspect_chunks(folder: Path) -> list[dict]:
    read_info(folder)
    rows = []
    for name in NAMES:
        path = folder / name
        if not path.is_file():
            raise BackupError(f"missing chunk {path}")
        size = path.stat().st_size
        if size != CHUNK:
            raise BackupError(f"{name}: size {size}, expected {CHUNK}")
        rows.append({"file": name, "size": size, "sha256": sha256_file(path)})
    return rows


def assemble(first: Path, second: Path, output: Path, serial: str) -> dict:
    if not re.fullmatch(r"[A-Za-z0-9._:-]{1,64}", serial):
        raise BackupError("invalid device serial format")
    if output.exists():
        raise BackupError(f"refusing to overwrite existing backup: {output}")
    manifest_path = Path(str(output) + ".manifest.json")
    if manifest_path.exists():
        raise BackupError(f"refusing to overwrite existing manifest: {manifest_path}")

    first_rows = inspect_chunks(first)
    second_rows = inspect_chunks(second)
    for a, b in zip(first_rows, second_rows):
        if a["sha256"] != b["sha256"]:
            raise BackupError(f"two independent reads differ at {a['file']}")

    output.parent.mkdir(parents=True, exist_ok=True)
    partial = Path(str(output) + ".partial")
    if partial.exists():
        raise BackupError(f"partial output already exists; inspect before removing: {partial}")
    whole = hashlib.sha256()
    try:
        with partial.open("xb") as dst:
            for row in first_rows:
                with (first / row["file"]).open("rb") as src:
                    while True:
                        block = src.read(1024 * 1024)
                        if not block:
                            break
                        dst.write(block)
                        whole.update(block)
            dst.flush()
            os.fsync(dst.fileno())
        if partial.stat().st_size != SIZE:
            raise BackupError(f"assembled size {partial.stat().st_size}, expected {SIZE}")
        assembled_hash = whole.hexdigest()
        second_hash = hashlib.sha256()
        for row in second_rows:
            with (second / row["file"]).open("rb") as src:
                for block in iter(lambda: src.read(1024 * 1024), b""):
                    second_hash.update(block)
        if assembled_hash != second_hash.hexdigest():
            raise BackupError("whole-image SHA-256 differs between independent reads")
        os.replace(partial, output)
    except Exception:
        if partial.exists():
            partial.unlink()
        raise

    manifest = {
        "schema_version": 1,
        "device": {"serial": serial, "product": "violet"},
        "partition": {
            "name": "boot",
            "index": 76,
            "path": "/dev/mmcblk0p76",
            "size_bytes": SIZE,
        },
        "acquisition": {
            "method": "QEAFRESCUE dump boot",
            "source_open_mode": "O_RDONLY",
            "chunk_size_bytes": CHUNK,
            "chunk_count": 4,
            "independent_reads": 2,
            "created_at_utc": datetime.now(timezone.utc).isoformat(),
        },
        "verification": {
            "chunk_sizes_exact": True,
            "per_chunk_sha256_match_between_reads": True,
            "whole_image_sha256_match_between_reads": True,
            "assembled_size_exact": output.stat().st_size == SIZE,
        },
        "chunks": first_rows,
        "backup": {"file": output.name, "size_bytes": SIZE, "sha256": assembled_hash},
    }
    tmp_manifest = Path(str(manifest_path) + ".partial")
    with tmp_manifest.open("x", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2, sort_keys=True)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp_manifest, manifest_path)
    return {"backup": str(output), "manifest": str(manifest_path),
            "size_bytes": SIZE, "sha256": assembled_hash,
            "two_pass_verified": True}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--first-pass", type=Path, required=True)
    p.add_argument("--second-pass", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--serial", required=True)
    a = p.parse_args()
    try:
        result = assemble(a.first_pass, a.second_pass, a.output, a.serial)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except (BackupError, OSError, ValueError) as exc:
        print(f"p76 backup verification FAILED: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
