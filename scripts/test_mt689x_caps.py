#!/usr/bin/env python3
"""Build and run the MT6893 capability test on the host.

The capability publish/report code is plain C with no device access, so it is
compiled for x86_64-linux and executed inside WSL (this machine has no native
Linux toolchain). The test source is tests/test_mt689x_caps.c.

    python scripts/test_mt689x_caps.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WSL_DISTRO = os.environ.get("QEAF_WSL_DISTRO", "Ubuntu-24.04")


def wsl_path(path):
    p = os.path.abspath(path).replace("\\", "/")
    if len(p) > 1 and p[1] == ":":
        return "/mnt/%s%s" % (p[0].lower(), p[2:])
    return p


def main():
    src = os.path.join(ROOT, "tests", "test_mt689x_caps.c")
    caps = os.path.join(ROOT, "platform", "mt689x", "mt689x_caps.c")
    registry = os.path.join(ROOT, "system", "core", "capability.c")
    out = "/tmp/qeaf_test_mt689x_caps"

    cmd = [
        "wsl", "-d", WSL_DISTRO, "--", "bash", "-lc",
        "gcc -std=c11 -D_GNU_SOURCE -Wall -Wextra -O1 -o %s %s %s %s "
        "-I%s -I%s -I%s && %s" % (
            out, wsl_path(src), wsl_path(caps), wsl_path(registry),
            wsl_path(os.path.join(ROOT, "platform", "mt689x")),
            wsl_path(os.path.join(ROOT, "platform", "violet")),
            wsl_path(os.path.join(ROOT, "system", "core", "include")),
            out),
    ]
    print("+ " + " ".join(cmd[:4]) + " ...")
    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
