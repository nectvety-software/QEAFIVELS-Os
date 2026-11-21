"""Capture real QEMU framebuffer screenshots of the VQEAF boot splash and UI.

Runs the normal UEFI image with a headless display, asks the QEMU monitor for
`screendump`, and converts the PPM to PNG using only the Python standard
library. Nothing is written to a physical device.

    python scripts/capture_screen.py
"""
import os, shutil, socket, struct, subprocess, sys, time, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"
QEMU = os.environ.get("VQEAF_QEMU") or shutil.which("qemu-system-x86_64") or r"C:\Program Files\QEMU\qemu-system-x86_64.exe"
SHARE = Path(r"C:\Program Files\QEMU\share")
CODE = SHARE / "edk2-x86_64-code.fd"
VARS = SHARE / "edk2-i386-vars.fd"
OUT = BUILD / "screens"
TEST_DISK = BUILD / "virtio-test.img"
AHCI_DISK = BUILD / "ahci-test.img"

def ppm_to_png(ppm_path, png_path):
    data = ppm_path.read_bytes()
    pos, fields = 0, []
    for _ in range(3):
        end = data.index(b"\n", pos)
        fields.append(data[pos:end])
        pos = end + 1
    if fields[0] != b"P6" or int(fields[2]) != 255:
        raise SystemExit("unexpected PPM header: " + repr(fields[:3]))
    w, h = (int(v) for v in fields[1].split())
    pixels = data[pos:pos + w * h * 3]
    if len(pixels) != w * h * 3:
        raise SystemExit("truncated PPM pixel data")
    raw = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    png_path.write_bytes(png)
    # The VQEAF accent is teal (0x35E0C0). Counting those pixels proves our
    # own drawing is on screen rather than firmware text or a blank buffer.
    teal = sum(1 for i in range(0, len(pixels), 3)
               if pixels[i] < 120 and pixels[i + 1] > 150 and pixels[i + 2] > 120)
    return w, h, teal, len(pixels) // 3

def wait_for_marker(log_path, marker, deadline):
    while time.monotonic() < deadline:
        if log_path.exists() and marker in log_path.read_text(errors="replace"):
            return True
        time.sleep(0.05)
    return False

def main():
    if not Path(QEMU).is_file() or not CODE.is_file() or not VARS.is_file():
        raise SystemExit("QEMU or OVMF firmware missing")
    if not TEST_DISK.is_file() or not AHCI_DISK.is_file():
        raise SystemExit("missing test disks; run scripts/make_test_disk.py first")
    OUT.mkdir(parents=True, exist_ok=True)
    vars_copy = BUILD / "qemu-vars-capture.fd"
    shutil.copyfile(VARS, vars_copy)
    serial_log = BUILD / "capture-serial.log"
    serial_log.unlink(missing_ok=True)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    cmd = [
        str(QEMU), "-machine", "q35", "-m", "512M", "-vga", "std",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={CODE.as_posix()}",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy.as_posix()}",
        "-drive", f"file={(DIST/'vqeaf-x86_64-uefi.img').as_posix()},format=raw,if=ide,index=0",
        "-drive", f"file={TEST_DISK.as_posix()},format=raw,if=none,id=vqblk",
        "-device", "virtio-blk-pci,drive=vqblk,disable-modern=on,disable-legacy=off",
        "-drive", f"file={AHCI_DISK.as_posix()},format=raw,if=none,id=ahcidisk",
        "-device", "ide-hd,drive=ahcidisk,bus=ide.1",
        "-boot", "order=c", "-display", "none", "-serial", f"file:{serial_log.as_posix()}",
        "-monitor", f"tcp:127.0.0.1:{port},server,nowait", "-no-reboot", "-no-shutdown",
    ]
    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    # Wait on real serial milestones instead of guessing wall-clock timings:
    # the splash is on screen as soon as GOP is discovered, the shell as soon
    # as the kernel UI reports ready.
    # name, serial marker, extra seconds to wait (the UI capture is taken a
    # few seconds later so uptime/PIT counters visibly prove it is live).
    captures = [("splash", "SPLASH: terminal boot log ready", 0.0),
                ("ui", "UI: native shell online", 3.0)]
    results = []
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=5) as monitor:
            monitor.settimeout(0.5)
            try: monitor.recv(4096)
            except OSError: pass
            deadline = time.monotonic() + 20
            for name, marker, settle in captures:
                if not wait_for_marker(serial_log, marker, deadline):
                    raise SystemExit("timed out waiting for serial marker: " + marker)
                if name == "splash":
                    # The splash is intentionally brief. Freeze the guest at
                    # the exact marker so the screenshot cannot show the shell
                    # that starts a few hundred ms later.
                    monitor.sendall(b"stop\n")
                    time.sleep(0.2)
                if settle:
                    time.sleep(settle)
                # The UI repaints ~4x per second. A screenshot taken mid-clear
                # would show a torn frame, so take several and keep the most
                # complete one (highest count of VQEAF accent pixels).
                best = None
                for attempt in range(3):
                    ppm = OUT / f"{name}_try{attempt}.ppm"
                    ppm.unlink(missing_ok=True)
                    monitor.sendall(("screendump " + ppm.relative_to(ROOT).as_posix() + "\n").encode())
                    time.sleep(0.6)
                    if not ppm.is_file():
                        continue
                    candidate = OUT / f"{name}_try{attempt}.png"
                    w, h, teal, total = ppm_to_png(ppm, candidate)
                    if w >= 640 and (best is None or teal > best[3]):
                        best = (candidate, w, h, teal, total)
                if best is None or best[3] < 200:
                    raise SystemExit(f"{name}: no VQEAF drawing detected")
                if (best[1], best[2]) != (720, 1280):
                    raise SystemExit(f"{name}: expected the 9:16 portrait "
                                     f"720x1280 framebuffer, got {best[1]}x{best[2]}")
                png = OUT / f"{name}.png"
                shutil.copyfile(best[0], png)
                for attempt in range(3):
                    (OUT / f"{name}_try{attempt}.png").unlink(missing_ok=True)
                    (OUT / f"{name}_try{attempt}.ppm").unlink(missing_ok=True)
                results.append((name, png, best[1], best[2], best[3], best[4]))
                if name == "splash":
                    monitor.sendall(b"cont\n")
            monitor.sendall(b"quit\n")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=4)
            except subprocess.TimeoutExpired: proc.kill()
    log = serial_log.read_text(errors="replace")
    if "KERNEL PANIC" in log:
        raise SystemExit("kernel panicked during capture; see " + str(serial_log))
    for marker in ("DISPLAY: portrait 9:16", "GOP: framebuffer",
                   "UI: GOP framebuffer mapped", "UI: native shell online"):
        if marker not in log:
            raise SystemExit("missing serial marker: " + marker)
    for name, png, w, h, teal, total in results:
        print(f"CAPTURE {name}: {png}  {w}x{h}  VQEAF accent pixels={teal}/{total}")
    print("Serial log:", serial_log)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
