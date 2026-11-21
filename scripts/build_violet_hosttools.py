#!/usr/bin/env python3
"""Build the static aarch64-musl QRTR daemons QEAF PID 1 starts for Wi-Fi.

Sources are vendored (unmodified except one lzma stub) under
platform/violet/hosttools/src; upstream LICENSE files are kept alongside.

    python scripts/build_violet_hosttools.py

Outputs: build/violet/hosttools/{pd-mapper,tqftpserv}
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEAF = os.environ.get("QEAFDROPOS") or r"D:\QeafivelsOfficial\QeafdropOs"
ZIG = os.environ.get("ZIG") or os.path.join(
    QEAF, "out", "toolchain", "zig-windows-x86_64-0.13.0", "zig.exe")
HT = os.path.join(ROOT, "platform", "violet", "hosttools")
SRC = os.path.join(HT, "src")
OUT = os.path.join(ROOT, "build", "violet", "hosttools")

CFLAGS = ["-target", "aarch64-linux-musl", "-static", "-O2", "-g",
          "-Wall", "-Wextra", "-std=gnu11"]
INCLUDES = ["-I", HT + os.sep + "include", "-I",
            os.path.join(SRC, "qrtr", "include")]
QRTR_LIB = [os.path.join(SRC, "qrtr", "lib", f) for f in
            ("qrtr.c", "qmi.c", "logging.c")]

TARGETS = {
    "pd-mapper": [
        os.path.join(SRC, "pd-mapper", f) for f in
        ("pd-mapper.c", "assoc.c", "json.c", "servreg_loc.c")
    ] + [os.path.join(SRC, "lzma_stub.c")] + QRTR_LIB,
    "tqftpserv": [
        os.path.join(SRC, "tqftpserv", f) for f in
        ("tqftpserv.c", "translate.c")
    ] + QRTR_LIB,
    # rmtfs -P/-r keeps the modem's EFS in RAM; udev is replaced by a tiny
    # sysfs shim so the binary stays static. No EFS partition is written.
    "rmtfs": [
        os.path.join(SRC, "rmtfs", f) for f in
        ("qmi_rmtfs.c", "rmtfs.c", "rproc.c", "sharedmem.c",
         "storage.c", "util.c", "qmi_tlv.c")
    ] + [os.path.join(SRC, "udev_stub.c")] + QRTR_LIB,
    # Userspace WLFW handshake the downstream icnss v1 driver leaves out.
    "wlfwd": [os.path.join(SRC, "wlfwd", "wlfwd.c")],
    # Test tool from the same project: asks servreg (QMI 0x42) to restart a PD.
    "pdr-restart": [os.path.join(SRC, "wlfwd", "pdr-restart.c")],
    # Register a PDR listener for the WLAN protection domain.
    "pd-listen": [os.path.join(SRC, "wlfwd", "pd-listen.c")] + QRTR_LIB,
    # Ordinary QRTR client: ask the in-kernel name service for WLFW.
    "qrtr-lookup": [os.path.join(SRC, "qrtr", "qrtr-lookup.c")] + QRTR_LIB,
}


def main():
    if not os.path.isfile(ZIG):
        print("build_violet_hosttools: zig not found at %s" % ZIG,
              file=sys.stderr)
        return 1
    os.makedirs(OUT, exist_ok=True)
    for name, sources in TARGETS.items():
        for src in sources:
            if not os.path.isfile(src):
                print("build_violet_hosttools: missing %s" % src,
                      file=sys.stderr)
                return 1
        dest = os.path.join(OUT, name)
        cmd = [ZIG, "cc"] + CFLAGS + INCLUDES + sources + ["-o", dest]
        print("build_violet_hosttools: %s (%d sources)" % (name, len(sources)))
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.stdout:
            print(proc.stdout.rstrip())
        if proc.stderr:
            print(proc.stderr.rstrip(), file=sys.stderr)
        if proc.returncode:
            return proc.returncode
        if os.path.getsize(dest) < 4096:
            print("build_violet_hosttools: %s looks too small" % dest,
                  file=sys.stderr)
            return 1
    print("build_violet_hosttools: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
