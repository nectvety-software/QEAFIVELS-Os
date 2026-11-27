#!/usr/bin/env python3
"""Validate and optionally apply the reviewed Axion-derived Violet backports."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PATCH_DIR = ROOT / "platform" / "violet" / "kernel-patches" / "axion"
MANIFEST = PATCH_DIR / "provenance.json"
FORBIDDEN_PARTS = {"vendor", "hal", "proprietary-files.txt"}


def fail(message: str) -> None:
    raise SystemExit(f"violet-backports: {message}")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_child(root: Path, relative: str) -> Path:
    candidate = (root / relative).resolve()
    if candidate != root.resolve() and root.resolve() not in candidate.parents:
        fail(f"path escapes source tree: {relative}")
    return candidate


def run_git(source: Path, *args: str) -> None:
    result = subprocess.run(
        ["git", "-C", str(source), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode:
        fail(result.stdout.strip() or f"git {' '.join(args)} failed")


def validate_manifest() -> list[dict[str, object]]:
    if not MANIFEST.is_file():
        fail(f"missing manifest: {MANIFEST}")
    try:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"invalid manifest: {exc}")

    production = manifest.get("production_kernel", {})
    if production.get("source_revision") is not None:
        fail("production source provenance changed; review the manifest before use")
    if manifest.get("verification", {}).get("applied_to_production") is not False:
        fail("candidate patches must not be marked as applied to production")

    patches = manifest.get("patches")
    if not isinstance(patches, list) or not patches:
        fail("manifest has no patches")
    expected = {str(entry["file"]) for entry in patches}
    actual = {path.name for path in PATCH_DIR.glob("*.patch")}
    if actual != expected:
        fail(f"patch set mismatch: expected {sorted(expected)}, found {sorted(actual)}")

    for entry in patches:
        patch = checked_child(PATCH_DIR, str(entry["file"]))
        if sha256(patch) != entry.get("sha256"):
            fail(f"hash mismatch: {patch.name}")
        text = patch.read_text(encoding="ascii")
        paths = re.findall(r"^diff --git a/(.*?) b/(.*?)$", text, re.MULTILINE)
        if len(paths) != 1 or paths[0][0] != paths[0][1]:
            fail(f"unexpected diff paths in {patch.name}")
        relative = paths[0][0]
        if relative != entry.get("path"):
            fail(f"manifest path mismatch in {patch.name}")
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts:
            fail(f"unsafe patch path: {relative}")
        if FORBIDDEN_PARTS.intersection(part.lower() for part in path.parts):
            fail(f"Android/proprietary path is not allowed: {relative}")
        source_lines = [line for line in text.splitlines() if line.startswith("Backport-source:")]
        if not any(line.endswith(f"@{entry['axion_commit']}") for line in source_lines):
            fail(f"missing Axion commit provenance in {patch.name}")
        if not entry.get("postcondition"):
            fail(f"missing postcondition for {patch.name}")
    return patches


def check_source(source: Path, patches: list[dict[str, object]], allow_unverified: bool) -> None:
    source = source.resolve()
    if not source.is_dir() or not (source / ".git").exists():
        fail("--source must be a Git kernel worktree")

    for entry in patches:
        path = checked_child(source, str(entry["path"]))
        if not path.is_file():
            fail(f"missing source file: {path}")
        actual = sha256(path)
        expected = str(entry["micode_base_sha256"])
        if actual != expected and not allow_unverified:
            fail(
                f"unverified source for {entry['path']}: {actual}; "
                "use --allow-unverified-source only after manual provenance review"
            )
        print(f"source: {entry['path']} {actual}")

    for entry in patches:
        patch = checked_child(PATCH_DIR, str(entry["file"]))
        run_git(source, "apply", "--check", "--whitespace=error-all", str(patch))
        print(f"apply-check: PASS {patch.name}")


def apply_source(source: Path, patches: list[dict[str, object]]) -> None:
    for entry in patches:
        patch = checked_child(PATCH_DIR, str(entry["file"]))
        run_git(source, "apply", "--whitespace=error-all", str(patch))
        target = checked_child(source, str(entry["path"]))
        if str(entry["postcondition"]) not in target.read_text(encoding="utf-8", errors="replace"):
            fail(f"postcondition failed after {patch.name}")
        print(f"applied: {patch.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Git kernel worktree to check or patch")
    parser.add_argument("--apply", action="store_true", help="apply after all checks pass")
    parser.add_argument(
        "--allow-unverified-source",
        action="store_true",
        help="allow apply-check when file hashes differ from the pinned candidate base",
    )
    args = parser.parse_args()
    if args.apply and args.source is None:
        fail("--apply requires --source")
    if args.allow_unverified_source and args.source is None:
        fail("--allow-unverified-source requires --source")

    patches = validate_manifest()
    print(f"manifest: PASS ({len(patches)} patches)")
    if args.source is not None:
        check_source(args.source, patches, args.allow_unverified_source)
        if args.apply:
            apply_source(args.source, patches)
    return 0


if __name__ == "__main__":
    sys.exit(main())
