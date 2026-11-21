"""Drive the VQEAF Recovery menu end-to-end in QEMU.

Boots the standalone recovery image, waits for the GOP menu, then uses the QEMU
monitor `sendkey` (real PS/2 key presses, not synthetic serial bytes) to:
  1. open Apply Update  -> blocked installer view (no updater service)
  2. confirm a disabled entry (Wipe Data) -> reported as unsupported
  3. select Power Off   -> ACPI soft-off marker
A `screendump` of the live menu is saved as build/screens/recovery_menu.png.
Nothing is written to a physical device.

    python scripts/test_recovery_ui.py
"""
import os, shutil, socket, struct, subprocess, sys, time, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"
QEMU = os.environ.get("VQEAF_QEMU") or shutil.which("qemu-system-x86_64") or r"C:\Program Files\QEMU\qemu-system-x86_64.exe"
SHARE = Path(r"C:\Program Files\QEMU\share")
CODE = Path(os.environ.get("VQEAF_OVMF_CODE", str(SHARE / "edk2-x86_64-code.fd")))
VARS = Path(os.environ.get("VQEAF_OVMF_VARS", str(SHARE / "edk2-i386-vars.fd")))
OUT = BUILD / "screens"

def ppm_to_png(ppm_path, png_path):
    data = ppm_path.read_bytes()
    pos, fields = 0, []
    for _ in range(3):
        end = data.index(b"\n", pos)
        fields.append(data[pos:end])
        pos = end + 1
    if fields[0] != b"P6" or int(fields[2]) != 255:
        raise SystemExit("unexpected PPM header")
    w, h = (int(v) for v in fields[1].split())
    pixels = data[pos:pos + w * h * 3]
    raw = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))
    png_path.write_bytes(b"\x89PNG\r\n\x1a\n"
                         + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                         + chunk(b"IDAT", zlib.compress(raw, 6))
                         + chunk(b"IEND", b""))
    teal = sum(1 for i in range(0, len(pixels), 3)
               if pixels[i] < 120 and pixels[i + 1] > 150 and pixels[i + 2] > 120)
    return w, h, teal

def main():
    if not Path(QEMU).is_file() or not CODE.is_file() or not VARS.is_file():
        print("SKIP recovery UI: qemu-system-x86_64 or OVMF missing")
        return 0
    image = DIST / "vqeaf-x86_64-recovery.img"
    if not image.is_file():
        raise SystemExit("missing recovery image; run scripts/build.py")
    OUT.mkdir(parents=True, exist_ok=True)
    vars_copy = BUILD / "qemu-vars-recovery-ui.fd"
    shutil.copyfile(VARS, vars_copy)
    log = BUILD / "recovery-ui-serial.log"
    log.unlink(missing_ok=True)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    cmd = [
        str(QEMU), "-machine", "q35", "-m", "512M", "-vga", "std",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={CODE.as_posix()}",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy.as_posix()}",
        "-drive", f"file={image.as_posix()},format=raw,if=ide,index=0",
        "-boot", "order=c", "-display", "none", "-serial", f"file:{log.as_posix()}",
        "-monitor", f"tcp:127.0.0.1:{port},server,nowait", "-no-reboot", "-no-shutdown",
    ]
    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    def read_log():
        return log.read_text(errors="replace") if log.exists() else ""
    def wait_for(marker, timeout=40):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if marker in read_log():
                return True
            if proc.poll() is not None:
                break
            time.sleep(0.1)
        raise SystemExit("timed out waiting for recovery marker: " + marker + "\n" + read_log())
    def key(name):
        monitor.sendall(f"sendkey {name}\n".encode())
        time.sleep(0.45)
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=10) as monitor:
            monitor.settimeout(1)
            try: monitor.recv(4096)
            except OSError: pass
            wait_for("RECOVERY: menu ready (arrows + Enter)")
            time.sleep(0.6)
            ppm = OUT / "recovery_menu_live.ppm"
            ppm.unlink(missing_ok=True)
            monitor.sendall(("screendump " + ppm.relative_to(ROOT).as_posix() + "\n").encode())
            time.sleep(0.8)
            if not ppm.is_file():
                raise SystemExit("recovery screendump was not produced")
            w, h, teal = ppm_to_png(ppm, OUT / "recovery_menu.png")
            if (w, h) != (720, 1280):
                raise SystemExit(f"recovery menu: expected the 9:16 portrait "
                                 f"720x1280 framebuffer, got {w}x{h}")
            if teal < 200:
                raise SystemExit(f"recovery menu screenshot shows no VQEAF drawing ({w}x{h}, teal={teal})")
            print(f"CAPTURE recovery_menu: {OUT/'recovery_menu.png'}  {w}x{h}  accent={teal}")
            # Item 1: Apply Update -> blocked installer view, then any key returns.
            key("down")
            key("ret")
            wait_for("RECOVERY: update installer blocked (no service)")
            key("ret")
            time.sleep(0.4)
            # Item 2: Wipe Data is disabled; confirming it must not act.
            key("down")
            key("ret")
            wait_for("RECOVERY: disabled item ignored")
            # Walk to Power Off (index 5 from index 2: 3 more downs) and confirm.
            key("down"); key("down"); key("down")
            key("ret")
            wait_for("RECOVERY: action=poweroff via ACPI PM1a_CNT")
            monitor.sendall(b"quit\n")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill()
    for marker in ("VQEAF Recovery 0.3", "RECOVERY: GOP mode=", "portrait base=",
                   "RECOVERY: menu ready (arrows + Enter)",
                   "RECOVERY: action=apply-update -> blocked view",
                   "RECOVERY: disabled item ignored",
                   "RECOVERY: action=poweroff via ACPI PM1a_CNT"):
        if marker not in read_log():
            raise SystemExit("missing recovery marker: " + marker)
    print("Recovery menu, blocked update view, disabled items and power-off verified.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
