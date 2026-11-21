#!/usr/bin/env python3
"""Build and run the DRM mode-pick host test inside WSL.

    python scripts/test_mt689x_mode_pick.py

tests/shim/drm/drm_mode.h stands in for the UAPI header (no libdrm-dev on this
machine); the pick rule itself is the real platform/violet/drm_mode_pick.h.
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
    src = os.path.join(ROOT, "tests", "test_mt689x_mode_pick.c")
    out = "/tmp/qeaf_test_mt689x_mode_pick"
    cmd = [
        "wsl", "-d", WSL_DISTRO, "--", "bash", "-lc",
        "gcc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -o %s %s "
        "-I%s -I%s && %s" % (
            out, wsl_path(src),
            wsl_path(os.path.join(ROOT, "tests", "shim")),
            wsl_path(os.path.join(ROOT, "platform", "violet")),
            out),
    ]
    print("+ wsl gcc tests/test_mt689x_mode_pick.c")
    rc = subprocess.call(cmd)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
