#!/usr/bin/env python3
"""One-off ramfb verification probe (EMULATED QEMU virt, not violet hardware).

Boots the AArch64 kernel with -device ramfb and a QMP socket, waits for a UART
marker, issues `screendump`, and parses the resulting PPM. Used to independently
confirm the compositor's framebuffer is actually scanned out by QEMU. The
authoritative gate evidence is the kernel's own UART pixel readback.
"""
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QEMU = r"C:\Program Files\QEMU\qemu-system-aarch64.exe"


def main():
    marker = sys.argv[1] if len(sys.argv) > 1 else "test pattern ready"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 4450
    elf = ROOT / "build/kernel-aarch64/vqeaf-kernel-aarch64.elf"
    log = ROOT / "build/ramfb-uart.log"
    ppm = ROOT / "build/fb.ppm"
    log.unlink(missing_ok=True)
    ppm.unlink(missing_ok=True)
    cmd = [QEMU, "-machine", "virt", "-cpu", "cortex-a57", "-m", "128M",
           "-display", "none", "-serial", "file:" + str(log), "-monitor", "none",
           "-no-reboot", "-device", "ramfb",
           "-qmp", "tcp:127.0.0.1:%d,server=on,wait=off" % port, "-kernel", str(elf)]
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    s = None
    for _ in range(80):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=1)
            break
        except OSError:
            time.sleep(0.1)
    seen = False
    if s:
        f = s.makefile("rwb")
        try:
            json.loads(f.readline())
            f.write(b'{"execute":"qmp_capabilities"}\n')
            f.flush()
            json.loads(f.readline())
            for _ in range(250):
                if log.exists() and marker in log.read_text(errors="replace"):
                    seen = True
                    break
                time.sleep(0.1)
            f.write(b'{"execute":"screendump","arguments":{"filename":"'
                    + str(ppm).encode() + b'"}}\n')
            f.flush()
            print("screendump resp:", f.readline().decode().strip())
            time.sleep(0.5)
        except Exception as e:
            print("qmp error:", e)
        s.close()
    print("marker seen:", seen)
    for line in (log.read_text(errors="replace") if log.exists() else "").splitlines():
        if "DISPLAY" in line or "COMPOSITOR" in line:
            print(line)
    p.terminate()
    try:
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        p.kill()
    if ppm.exists():
        data = ppm.read_bytes()
        parts = data.split(b"\n", 3)
        w, h = map(int, parts[1].split())
        pix = parts[3]

        def at(x, y):
            o = (y * w + x) * 3
            return tuple(pix[o:o + 3])
        print("PPM %dx%d" % (w, h))
        print("left-quarter=", at(w // 4, h // 2),
              "right-quarter=", at(3 * w // 4, h // 2),
              "center=", at(w // 2, h // 2))
        if w >= 64 and h >= 48:
            print("surface samples: red(8,8)=", at(8, 8),
                  "teal(24,8)=", at(24, 8), "bg(40,40)=", at(40, 40))
        return 0
    print("NO PPM")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())