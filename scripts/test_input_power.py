"""Drive the VQEAF phone shell and display sleep/wake end-to-end in QEMU.

The kernel polls COM1, so the guest is driven over a QEMU serial socket
chardev (real bytes into the emulated 16550, not monitor sendkey):
  1. boot lands on the lock screen, CMOS RTC shown in the status bar
  2. E unlocks, WASD moves the launcher grid, B goes back
  3. settings toggles the timeout, storage/clock/about open and render
  4. Q opens quick settings, ENTER on "Tat man hinh" blanks the framebuffer
  5. any key wakes and restores the shell
  6. the UPDATE tile shows the blocked "Installing System Update" view
  7. long P opens the power menu over the shell; Power Off halts QEMU
Screenshots are saved under build/screens and gated on pixel content.

    python scripts/test_input_power.py
"""
import os, re, shutil, socket, struct, subprocess, sys, time, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"
OUT = BUILD / "screens"
TEST_DISK = BUILD / "virtio-test.img"
AHCI_DISK = BUILD / "ahci-test.img"
QEMU = os.environ.get("VQEAF_QEMU") or shutil.which("qemu-system-x86_64") or r"C:\Program Files\QEMU\qemu-system-x86_64.exe"
SHARE = Path(r"C:\Program Files\QEMU\share")
CODE = Path(os.environ.get("VQEAF_OVMF_CODE", str(SHARE / "edk2-x86_64-code.fd")))
VARS = Path(os.environ.get("VQEAF_OVMF_VARS", str(SHARE / "edk2-i386-vars.fd")))


def free_ports(count):
    sockets = [socket.socket() for _ in range(count)]
    try:
        for probe in sockets:
            probe.bind(("127.0.0.1", 0))
        return [probe.getsockname()[1] for probe in sockets]
    finally:
        for probe in sockets:
            probe.close()


def read_ppm(path):
    data = path.read_bytes()
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
    return w, h, pixels


def save_png(w, h, pixels, png_path):
    raw = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))
    png_path.write_bytes(b"\x89PNG\r\n\x1a\n"
                         + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                         + chunk(b"IDAT", zlib.compress(raw, 6))
                         + chunk(b"IEND", b""))


def teal(pixels):
    return sum(1 for i in range(0, len(pixels), 3)
               if pixels[i] < 120 and pixels[i + 1] > 150 and pixels[i + 2] > 120)


def bright(pixels):
    # Glyph/label pixels (0xEAFBFF). A torn repaint keeps the backdrop and the
    # selection ring but loses later rows, so bright count separates complete
    # frames from torn ones where the teal count ties.
    return sum(1 for i in range(0, len(pixels), 3)
               if pixels[i] > 180 and pixels[i + 1] > 180 and pixels[i + 2] > 180)


def nonblack(pixels):
    return sum(1 for b in pixels if b)


def diff(a, b):
    # Sample every 7th pixel; whole-screen layout changes are far above the gate.
    return sum(1 for i in range(0, min(len(a), len(b)), 21) if a[i:i + 3] != b[i:i + 3])


def main():
    if not Path(QEMU).is_file() or not CODE.is_file() or not VARS.is_file():
        print("SKIP phone UI input/power: qemu-system-x86_64 or OVMF missing")
        return 0
    if not TEST_DISK.is_file() or not AHCI_DISK.is_file():
        raise SystemExit("missing test disks; run scripts/build.py")
    image = DIST / "vqeaf-x86_64-uefi.img"
    if not image.is_file():
        raise SystemExit("missing image; run scripts/build.py")
    OUT.mkdir(parents=True, exist_ok=True)
    vars_copy = BUILD / "qemu-vars-input-power.fd"
    shutil.copyfile(VARS, vars_copy)
    ser_port, mon_port = free_ports(2)
    cmd = [
        str(QEMU), "-machine", "q35", "-m", "512M", "-vga", "std",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={CODE.as_posix()}",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy.as_posix()}",
        "-drive", f"file={image.as_posix()},format=raw,if=ide,index=0",
        "-drive", f"file={TEST_DISK.as_posix()},format=raw,if=none,id=vqblk",
        "-device", "virtio-blk-pci,drive=vqblk,disable-modern=on,disable-legacy=off",
        "-drive", f"file={AHCI_DISK.as_posix()},format=raw,if=none,id=ahcidisk",
        "-device", "ide-hd,drive=ahcidisk,bus=ide.1",
        "-boot", "order=c", "-display", "none",
        "-chardev", f"socket,id=vser,host=127.0.0.1,port={ser_port},server=on,wait=off,nodelay=on",
        "-serial", "chardev:vser",
        "-monitor", f"tcp:127.0.0.1:{mon_port},server,nowait", "-no-reboot", "-no-shutdown",
    ]
    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    serial_buf = ""
    ser = None

    def connect_serial(timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise SystemExit("QEMU exited before serial connect:\n"
                                 + proc.stderr.read().decode(errors="replace"))
            try:
                s = socket.create_connection(("127.0.0.1", ser_port), timeout=1)
                s.settimeout(0.2)
                return s
            except OSError:
                time.sleep(0.1)
        raise SystemExit("could not connect to QEMU serial port")

    def pump_until(marker, timeout=40):
        nonlocal serial_buf
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = ser.recv(4096)
                if data:
                    serial_buf += data.decode(errors="replace")
            except socket.timeout:
                pass
            except OSError:
                break
            if marker in serial_buf:
                return
            if proc.poll() is not None:
                break
            time.sleep(0.05)
        raise SystemExit("timed out waiting for serial marker: " + marker
                         + "\n--- serial tail ---\n" + serial_buf[-4000:])

    def pump_regex(pattern, timeout=20):
        # Line markers arrive split across reads; wait for the whole shape.
        nonlocal serial_buf
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data = ser.recv(4096)
                if data:
                    serial_buf += data.decode(errors="replace")
            except socket.timeout:
                pass
            except OSError:
                break
            found = re.search(pattern, serial_buf)
            if found:
                return found.group(0)
            if proc.poll() is not None:
                break
            time.sleep(0.05)
        raise SystemExit("timed out waiting for serial pattern: " + pattern
                         + "\n--- serial tail ---\n" + serial_buf[-4000:])

    def wait_count(marker, count, timeout=10):
        nonlocal serial_buf
        deadline = time.monotonic() + timeout
        eof = False
        while serial_buf.count(marker) < count and time.monotonic() < deadline:
            try:
                data = ser.recv(4096)
                if data:
                    serial_buf += data.decode(errors="replace")
                else:
                    eof = True
                    break
            except socket.timeout:
                pass
            except OSError:
                eof = True
                break
            time.sleep(0.05)
        if serial_buf.count(marker) < count:
            status = proc.poll()
            stderr = ""
            if status is not None:
                try:
                    stderr = proc.stderr.read().decode(errors="replace")[-2000:]
                except OSError:
                    stderr = ""
            regs = ""
            try:
                for _ in range(2):
                    monitor.sendall(b"info registers\n")
                    time.sleep(0.5)
                monitor.settimeout(1)
                while True:
                    try:
                        chunk = monitor.recv(65536).decode(errors="replace")
                    except socket.timeout:
                        break
                    if not chunk:
                        break
                    regs += chunk
                    if "(qemu)" in regs:
                        break
            except OSError:
                pass
            raise SystemExit(f"marker {marker!r} seen {serial_buf.count(marker)}x, "
                             f"expected {count}, qemu exit={status}, serial eof={eof}\n{stderr}"
                             f"--- guest registers ---\n{regs}"
                             f"--- serial tail ---\n" + serial_buf[-4000:])

    def send(text):
        ser.sendall(text.encode())

    def expect_view(name, timeout=10):
        # The kernel logs "PHONE: view=<name>" when a key changes the shell
        # screen; waiting on the marker removes the fixed-sleep race where the
        # repaint landed before the unlock key was processed.
        marker = f"PHONE: view={name}"
        wait_count(marker, serial_buf.count(marker) + 1, timeout)

    def shot(name, attempts=1, settle=0.9):
        # The shell repaints every 250 ms; screendump captures immediately, so
        # let several repaints land after the serial marker first.
        time.sleep(settle)
        best = None
        for attempt in range(attempts):
            ppm = OUT / f"{name}_try{attempt}.ppm"
            ppm.unlink(missing_ok=True)
            monitor.sendall(("screendump " + ppm.relative_to(ROOT).as_posix() + "\n").encode())
            time.sleep(0.7)
            if not ppm.is_file():
                continue
            w, h, pixels = read_ppm(ppm)
            candidate = (w, h, pixels, ppm)
            if best is None:
                best = candidate
            elif name == "screen_asleep":
                if nonblack(pixels) < nonblack(best[2]):
                    best = candidate
            elif teal(pixels) + bright(pixels) > teal(best[2]) + bright(best[2]):
                best = candidate
        for attempt in range(attempts):
            (OUT / f"{name}_try{attempt}.ppm").unlink(missing_ok=True)
        if best is None:
            raise SystemExit("screendump was not produced: " + name)
        w, h, pixels, _ = best
        if (w, h) != (720, 1280):
            raise SystemExit(f"{name}: expected the 9:16 portrait 720x1280 "
                             f"framebuffer, got {w}x{h}")
        png = OUT / f"{name}.png"
        save_png(w, h, pixels, png)
        return w, h, pixels, png

    try:
        ser = connect_serial()
        with socket.create_connection(("127.0.0.1", mon_port), timeout=10) as monitor:
            monitor.settimeout(1)
            try: monitor.recv(4096)
            except OSError: pass
            pump_until("PHONE: W/A/S/D move, E select, B back, Q quick, L lock", 60)
            pump_until("UI: native shell online")
            pump_regex(r"RTC: CMOS 20\d\d-\d\d-\d\d \d\d:\d\d:\d\d", 20)

            w, h, lock, lock_png = shot("phone_lock")
            if w < 640 or teal(lock) < 200:
                raise SystemExit(f"lock screen is not drawn ({w}x{h})")

            send("e")
            expect_view("home", 20)
            w, h, home, home_png = shot("phone_home", attempts=3)
            if teal(home) < 200:
                raise SystemExit("unlock did not draw the home screen")
            if diff(lock, home) < 4000:
                raise SystemExit("unlock did not replace the lock screen")

            send("e")  # ABOUT tile is selected first
            time.sleep(0.4)
            w, h, about, about_png = shot("phone_about")
            if diff(home, about) < 2000:
                raise SystemExit("about screen did not replace the launcher")
            send("b")
            time.sleep(0.4)

            send("d")
            send("e")  # SETTINGS (index 1)
            time.sleep(0.4)
            w, h, settings, settings_png = shot("phone_settings")
            if diff(home, settings) < 2000:
                raise SystemExit("settings screen did not replace the launcher")
            send("e")  # cycle screen timeout
            pump_until("PHONE: timeout=")
            send("b")
            time.sleep(0.4)

            send("d")
            send("e")  # STORAGE (index 2)
            time.sleep(0.4)
            w, h, storage, storage_png = shot("phone_storage")
            if diff(home, storage) < 2000:
                raise SystemExit("storage screen did not replace the launcher")
            send("b")
            time.sleep(0.4)

            send("d")
            send("e")  # UPDATE (index 3 on the 4-column home)
            pump_until("UPDATE: blocked view shown (no updater service)")
            time.sleep(0.4)
            w, h, update, update_png = shot("update_blocked", attempts=2)
            if teal(update) < 200:
                raise SystemExit("blocked update screen has no VQEAF drawing")
            if diff(home, update) < 4000:
                raise SystemExit("update view did not replace the launcher")
            send("b")
            pump_until("UPDATE: view hidden")
            time.sleep(0.4)

            send("s")
            send("a")
            send("e")  # CLOCK (index 6: second row, third column)
            time.sleep(0.4)
            w, h, clock, clock_png = shot("phone_clock")
            if diff(home, clock) < 2000:
                raise SystemExit("clock screen did not replace the launcher")
            send("b")
            time.sleep(0.4)

            send("q")
            time.sleep(0.4)
            w, h, quick, quick_png = shot("phone_quick")
            if diff(home, quick) < 2000:
                raise SystemExit("quick settings did not replace the launcher")

            send("s")
            send("e")  # row 1 cycles the timeout
            wait_count("PHONE: timeout=", 2)
            send("s")
            send("e")  # row 2 "Tat man hinh"
            pump_until("PHONE: sleep")
            pump_until("POWER: display=off")
            time.sleep(0.8)
            w, h, asleep, asleep_png = shot("screen_asleep", attempts=3)
            black = nonblack(asleep)
            if black > 32:
                raise SystemExit(f"sleep did not blank the framebuffer (non-black bytes={black})")

            send("w")
            pump_until("POWER: wake from display-off")
            pump_until("POWER: display=active")
            w, h, awake, awake_png = shot("screen_awake")
            if teal(awake) < 200:
                raise SystemExit("wake did not restore the phone shell")
            if diff(asleep, awake) < 5000:
                raise SystemExit("wake framebuffer matches the blanked frame")

            send("b")
            time.sleep(0.4)
            send("P")
            pump_until("PMENU: open highlight=Power off")
            w, h, menu, menu_png = shot("power_menu")
            if teal(menu) < 200:
                raise SystemExit("power menu screenshot has no VQEAF drawing")
            if diff(home, menu) < 2000:
                raise SystemExit("power menu overlay did not change the framebuffer")

            send("sss")
            pump_until("PMENU: open highlight=Sleep")
            time.sleep(0.6)
            w, h, menu_sleep, _ = shot("power_menu_sleep")
            if diff(menu, menu_sleep) < 300:
                raise SystemExit("highlight move was not repainted")

            send("e")
            pump_until("PMENU: action=Sleep")
            pump_until("POWER: display=off")
            send("w")
            pump_until("POWER: display=active")
            time.sleep(0.6)

            send("P")
            pump_until("PMENU: open highlight=Power off")
            send("e")
            pump_until("PMENU: action=PowerOff; system halt")
            monitor.sendall(b"quit\n")
    finally:
        if ser is not None:
            ser.close()
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill()

    if "KERNEL PANIC" in serial_buf:
        raise SystemExit("kernel panicked during phone UI input/power test")
    for marker in ("PHONE: W/A/S/D move, E select, B back, Q quick, L lock",
                   "UI: native shell online", "RTC: CMOS 20",
                   "PHONE: view=home", "PHONE: view=app", "PHONE: view=quick",
                   "PHONE: timeout=", "PHONE: sleep",
                   "POWER: display=off", "POWER: wake from display-off",
                   "POWER: display=active", "UPDATE: blocked view shown (no updater service)",
                   "UPDATE: view hidden", "PMENU: action=Sleep",
                   "PMENU: action=PowerOff; system halt"):
        if marker not in serial_buf:
            raise SystemExit("missing phone UI marker: " + marker)
    print(f"CAPTURE phone_lock:     {lock_png}")
    print(f"CAPTURE phone_home:     {home_png}  (lock->home diff={diff(lock, home)})")
    print(f"CAPTURE phone_about:    {about_png}")
    print(f"CAPTURE phone_settings: {settings_png}")
    print(f"CAPTURE phone_storage:  {storage_png}")
    print(f"CAPTURE phone_clock:    {clock_png}")
    print(f"CAPTURE phone_quick:    {quick_png}")
    print(f"CAPTURE screen_asleep:  {asleep_png}  (non-black bytes={nonblack(asleep)})")
    print(f"CAPTURE screen_awake:   {awake_png}")
    print(f"CAPTURE update_blocked: {update_png}")
    print(f"CAPTURE power_menu:     {menu_png}")
    print("Phone shell, settings, quick sleep/wake, blocked update view and power-off verified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
