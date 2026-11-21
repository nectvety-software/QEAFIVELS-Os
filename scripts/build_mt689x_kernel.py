#!/usr/bin/env python3
"""Build the MT6891/MT6893 kernel from MT6891-Mainline/linux.

The tree lives outside this repository (it is a full Linux checkout), so this
script is a thin, explicit front end to scripts/mt689x_kernel_build.sh:

    Windows: runs the script inside WSL Ubuntu-24.04 (the D: drive is too full
             for a kernel build tree)
    Linux:   runs it directly

Examples:
    python scripts/build_mt689x_kernel.py --config-only
    python scripts/build_mt689x_kernel.py --jobs 12
    python scripts/build_mt689x_kernel.py --copy-to dist/mt689x-kernel
"""
import argparse
import os
import shutil
import subprocess
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "scripts", "mt689x_kernel_build.sh")
WSL_DISTRO = os.environ.get("QEAF_WSL_DISTRO", "Ubuntu-24.04")
KERNEL_OUT = os.environ.get("MTK_KERNEL_OUT",
                            r"\\wsl.localhost\Ubuntu-24.04\root\qeaf-mt689x\out\chopin")


def run(cmd, env=None):
    print("+ " + " ".join(cmd))
    return subprocess.call(cmd, env=env)


def wsl_path(path):
    """Translate D:\\x/y to /mnt/d/x/y for the WSL command line."""
    p = os.path.abspath(path).replace("\\", "/")
    if len(p) > 1 and p[1] == ":":
        return "/mnt/%s%s" % (p[0].lower(), p[2:])
    return p


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--defconfig", default="chopin")
    ap.add_argument("--jobs", default=str(os.cpu_count() or 4))
    ap.add_argument("--config-only", action="store_true")
    ap.add_argument("--copy-to", default=None,
                    help="copy Image/dtb/modules there after a successful build")
    args = ap.parse_args(argv)

    if not os.path.isfile(SCRIPT):
        print("build_mt689x_kernel: missing %s" % SCRIPT, file=sys.stderr)
        return 2

    env = dict(os.environ)
    if args.config_only:
        env["CONFIG_ONLY"] = "1"

    if os.name == "nt":
        cmd = ["wsl", "-d", WSL_DISTRO, "--", "env"]
        for key in ("CONFIG_ONLY",):
            if key in env:
                cmd.append("%s=%s" % (key, env[key]))
        cmd += ["bash", wsl_path(SCRIPT), args.defconfig, args.jobs]
        rc = run(cmd)
    else:
        rc = run(["bash", SCRIPT, args.defconfig, args.jobs],
                 env=env)
    if rc != 0:
        print("build_mt689x_kernel: kernel build failed (%d)" % rc,
              file=sys.stderr)
        return rc

    if args.copy_to:
        dest = os.path.abspath(args.copy_to)
        os.makedirs(dest, exist_ok=True)
        src = KERNEL_OUT
        pairs = [
            (os.path.join(src, "arch", "arm64", "boot", "Image"), "Image"),
            (os.path.join(src, "arch", "arm64", "boot", "dts", "mediatek",
                          "mt6893-xiaomi-chopin.dtb"),
             "mt6893-xiaomi-chopin.dtb"),
        ]
        for path, name in pairs:
            if os.path.isfile(path):
                shutil.copy2(path, os.path.join(dest, name))
                print("copied %s" % os.path.join(dest, name))
            else:
                print("missing %s" % path, file=sys.stderr)
        modules = os.path.join(src, "modules")
        if os.path.isdir(modules):
            target = os.path.join(dest, "modules")
            shutil.rmtree(target, ignore_errors=True)
            shutil.copytree(modules, target)
            print("copied modules -> %s" % target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
