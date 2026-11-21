#!/usr/bin/env python3
"""QEAPP-Engine: independent CLI wrapper around the QEAF SDK compiler."""
import argparse
import importlib.util
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
QEAF = ROOT / "sdk" / "cli" / "qeaf.py"

def load_qeaf():
    spec = importlib.util.spec_from_file_location("qeaf", QEAF)
    qeaf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qeaf)
    return qeaf

def main(argv=None):
    parser = argparse.ArgumentParser(prog="qeapp-engine",
                                     description="Build and manage QEAPP projects")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("create").add_argument("project")
    b = sub.add_parser("build")
    b.add_argument("project")
    b.add_argument("--config", default="all")
    v = sub.add_parser("validate")
    v.add_argument("package")
    args = parser.parse_args(argv)
    qeaf = load_qeaf()
    old = sys.argv
    try:
        if args.command == "create":
            sys.argv = ["qeaf", "create", args.project]
            return qeaf.main()
        if args.command == "build":
            sys.argv = ["qeaf", "build", args.project, "--config", args.config]
            return qeaf.main()
        if args.command == "validate":
            sys.argv = ["qeaf", "validate", args.package]
            return qeaf.main()
    finally:
        sys.argv = old

if __name__ == "__main__":
    raise SystemExit(main())
