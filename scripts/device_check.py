#!/usr/bin/env python3
"""scripts/device_check.py - one command: flash, drive the shell, compare, judge.

The phone exposes a rescue LUN (QEAFRESCUE) that answers verbs written to
QEAF.CMD and writes back RESPONSE.TXT, SCREEN.RAW and friends. This turns that
channel into a repeatable device check:

    python scripts/device_check.py --flash --scenario power_menu
    python scripts/device_check.py --steps "unlock;page settings;snap s;page files;snap f;expect diff s f"

Every step either acts on the phone or asserts something about what came back:
screenshots are identified by SHA-256 and compared by the share of differing
pixels, so "the page did not change" and "the menu appeared" become PASS/FAIL
instead of a human squinting at a PNG.

Steps (semicolon separated, `--steps`, or a built-in `--scenario`):

    wake                     wake the screen (power-key press if asleep)
    unlock                   rescue `unlock` verb: wake to Home without a swipe
    page NAME                rescue `page`: home, apps, files, settings
    tap X Y                  inject a touch at canvas pixels (720x1560)
    swipe X0 Y0 X1 Y1 [N]    synthetic drag, N points (default 20)
    key power [ms]           short power-key press (screen off/on)
    cmd VERB ...             any other rescue verb
    snap NAME                capture SCREEN.RAW -> <out>/NAME.png, remember hash
    expect same A B          hashes of A and B match
    expect diff A B          hashes differ
    expect resp TEXT         the last answer contains TEXT
    expect file NAME TEXT    a file on the volume contains TEXT
    sleep MS                 wait between steps
    note TEXT                print a line (keeps long scenarios readable)

All reads go through raw sectors: the mounted Windows view of this volume lags
behind the phone (it has shown an old RESPONSE.TXT while the device had
already answered), so the FAT16 root directory and cluster chains are parsed
here instead. The only things written to the volume are QEAF.CMD and - for
--flash - BOOTIMG.BIN.

--flash keeps fastboot out of the loop: it copies the image onto the drive
and sends `flash recovery BOOTIMG.BIN confirm`, so the phone writes p77
itself, verifies it and reboots. A fragmented BOOTIMG.BIN is fine (the verb
walks the cluster chain); fastboot is only the fallback for a phone that is
neither in QEAFRESCUE nor reachable through adb.

Exit code: 0 = every expectation held, 1 = a check failed, 2 = setup problem.
"""
import argparse
import ctypes
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE = os.path.join(ROOT, "dist", "boot-vqeaf-violet.img")
OUT_DIR = os.path.join(ROOT, "build", "screens", "device", "check")
RAW_W, RAW_H = 240, 520        # SCREEN.RAW geometry from fs/rescue.h
# Boot-stage codes carried in the SCREEN.RAW trailer (platform/violet/
# main_violet.c boot_stage_code). Anything before 's' is a stage where the
# splash has not been painted yet, so a black frame then means "captured too
# early", never "the renderer is broken".
STAGE_CODES = {"0": "start", "l": "lun", "w": "dri-wait", "b": "backlight",
               "d": "drm-init", "s": "splash", "p": "phone-ui", "o": "loop"}
PRE_PAINT_STAGES = {"start", "lun", "dri-wait", "backlight", "drm-init"}
SCALE = 3                      # 240x520 -> 720x1560, the canvas geometry

# --------------------------------------------------------------------------
# RAW volume access


def read_volume(letter, limit=64 * 1024 * 1024):
    """Whole QEAFRESCUE image through \\\\.\\X:, bypassing the mounted view."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateFileW.restype = wintypes.HANDLE
    k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD,
                                wintypes.DWORD, wintypes.LPVOID,
                                wintypes.DWORD, wintypes.DWORD,
                                wintypes.HANDLE]
    k32.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                             ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    handle = k32.CreateFileW("\\\\.\\" + letter, 0x80000000, 0x1 | 0x2,
                             None, 3, 0, None)
    if handle == wintypes.HANDLE(-1).value:
        raise OSError(ctypes.get_last_error(), "cannot open \\\\.\\" + letter)
    image = bytearray()
    chunk = 1 << 20
    try:
        while len(image) < limit:
            buf = (ctypes.c_char * chunk)()
            got = wintypes.DWORD(0)
            if not k32.ReadFile(handle, buf, chunk, ctypes.byref(got), None):
                break
            if got.value == 0:
                break
            image += buf.raw[:got.value]
            if got.value < chunk:
                break
    finally:
        k32.CloseHandle(handle)
    return bytes(image)


def _u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def _u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


class Volume:
    """Just enough FAT16 for the rescue root directory."""

    def __init__(self, image):
        boot = image[:512]
        self.bps = _u16(boot, 11)
        self.spc = boot[13]
        reserved = _u16(boot, 14)
        fats = boot[16]
        root_entries = _u16(boot, 17)
        per_fat = _u16(boot, 22)
        if self.bps not in (512, 1024, 2048, 4096) or not self.spc:
            raise ValueError("not a FAT volume")
        self.image = image
        self.fat_off = reserved * self.bps
        self.root_off = (reserved + fats * per_fat) * self.bps
        root_sectors = (root_entries * 32 + self.bps - 1) // self.bps
        self.data_off = self.root_off + root_sectors * self.bps

    def _cluster(self, cluster):
        return self.data_off + (cluster - 2) * self.spc * self.bps

    def _next(self, cluster):
        return _u16(self.image, self.fat_off + cluster * 2)

    def read(self, name):
        """File bytes, or None when the name is not in the root directory."""
        want = name.upper()
        for i in range((self.data_off - self.root_off) // 32):
            entry = self.image[self.root_off + i * 32:self.root_off + i * 32 + 32]
            if entry[0] == 0x00:
                break
            if entry[0] == 0xE5 or entry[11] == 0x0F or entry[11] & 0x08:
                continue
            stem = entry[:8].decode("ascii", "replace").rstrip()
            ext = entry[8:11].decode("ascii", "replace").rstrip()
            short = (stem + "." + ext) if ext else stem
            if short.upper() != want:
                continue
            data = bytearray()
            size = _u32(entry, 28)
            cluster = _u16(entry, 26)
            while 2 <= cluster < 0xFFF8 and len(data) < size:
                at = self._cluster(cluster)
                data += self.image[at:at + self.spc * self.bps]
                cluster = self._next(cluster)
            return bytes(data[:size])
        return None

    def contains(self, name):
        return self.read(name) is not None


def drive_letters():
    """Letters Windows currently has a volume mounted on (A: .. Z:)."""
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    return [chr(ord("A") + i) + ":" for i in range(26) if mask & (1 << i)]


def is_rescue_volume(letter):
    """True when the raw volume has a FAT root holding the QEAF marker file.

    Only the first 16 MiB are read: the rescue LUN is a small fixed image and
    both the root directory and SCREEN.RAW sit near its start, so scanning
    every fixed disk stays cheap.
    """
    try:
        return Volume(read_volume(letter, 16 * 1024 * 1024)).contains("SCREEN.RAW")
    except (OSError, ValueError, struct.error):
        return False


def find_rescue_volume(timeout=0.0, letters=None, scan=None):
    """First mounted volume that looks like QEAFRESCUE, or None.

    `letters` overrides the drive list (tests), `scan` the probe predicate.
    Polls until `timeout` seconds have passed because a phone that was just
    plugged in needs a moment before Windows mounts its FAT volume.
    """
    scan = scan or is_rescue_volume
    deadline = time.time() + max(0.0, timeout)
    while True:
        for letter in (letters if letters is not None else drive_letters()):
            if scan(letter):
                return letter
        if time.time() >= deadline:
            return None
        time.sleep(2)


# --------------------------------------------------------------------------
# PNG (no third-party dependency)


def fnv1a(data):
    """Same 32-bit FNV-1a the device stamps into the SCREEN.RAW trailer."""
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def write_png(path, width, height, rgb):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgb[y * width * 3:(y + 1) * width * 3]

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def raw_to_png(raw, path):
    """240x520 RGB565 -> 720x1560 RGB888 PNG (nearest neighbour)."""
    pixels = bytearray(RAW_W * SCALE * RAW_H * SCALE * 3)
    for y in range(RAW_H):
        for x in range(RAW_W):
            value = _u16(raw, (y * RAW_W + x) * 2)
            r = ((value >> 11) & 0x1F) * 255 // 31
            g = ((value >> 5) & 0x3F) * 255 // 63
            b = (value & 0x1F) * 255 // 31
            for dy in range(SCALE):
                row = (y * SCALE + dy) * RAW_W * SCALE
                for dx in range(SCALE):
                    at = (row + x * SCALE + dx) * 3
                    pixels[at] = r
                    pixels[at + 1] = g
                    pixels[at + 2] = b
    write_png(path, RAW_W * SCALE, RAW_H * SCALE, bytes(pixels))
    return bytes(pixels)


# --------------------------------------------------------------------------
# The phone


class Device:
    def __init__(self, args):
        self.args = args
        # "" means auto-detect: wait_ready() finds the QEAFRESCUE volume.
        self.letter = args.volume.rstrip("\\/:") + ":" if args.volume else ""
        self.hashes = {}
        self.frames = {}
        self.seq = 0
        self.frame = 0
        self.last_response = ""
        # A fresh host process must not reuse devcheck1: an old raw FAT
        # RESPONSE.TXT can still contain that nonce and satisfy send() early.
        self.counter = int(time.time() * 1000)

    # -- plumbing ------------------------------------------------------
    def image(self):
        return read_volume(self.letter)

    def wait_ready(self, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if not self.letter:
                found = find_rescue_volume(
                    timeout=max(0.0, deadline - time.time()))
                if not found:
                    return False
                self.letter = found
            try:
                if Volume(self.image()).contains("SCREEN.RAW"):
                    # The rescue LUN may expose an old response before the
                    # current boot has processed anything. Start after the
                    # sequence already present so send() waits for new work.
                    match = re.search(r"(?m)^seq=(\d+)$", self.response_text())
                    if match:
                        self.seq = int(match.group(1))
                    return True
            except (OSError, ValueError):
                pass
            time.sleep(2)
        return False

    def probe(self, timeout=20.0):
        """Round-trip proof that the rescue worker still services verbs.

        wait_ready() only shows the LUN is enumerated. A boot that hung at the
        splash stage leaves the volume mounted while nothing reads QEAF.CMD, so
        a flash staged there would never be written - and would burn its whole
        timeout before failing. Ask for an answer first."""
        if not self.letter:
            return False
        # RESPONSE.TXT from an earlier boot already reads "### status", so the
        # only proof of a live worker is send() moving the sequence on.
        before = self.seq
        self.send(["status"], wait=timeout)
        return self.seq > before

    def flush(self):
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Write-VolumeCache -DriveLetter %s"
                        % self.letter.rstrip(":")],
                       capture_output=True)

    def response_text(self):
        """RESPONSE.TXT through raw sectors, empty while the phone is away."""
        try:
            raw = Volume(self.image()).read("RESPONSE.TXT")
        except (OSError, ValueError):
            return ""
        return (raw or b"").split(b"\x00")[0].decode("utf-8", "replace")

    def send(self, lines, wait=8.0, expect=None):
        """Write one QEAF.CMD batch and wait for its own nonce answer."""
        self.counter += 1
        nonce = "devcheck%d" % self.counter
        batch = list(lines) + ["echo " + nonce]
        try:
            with open(os.path.join(self.letter + "\\", "QEAF.CMD"), "wb") as fh:
                fh.write(("\r\n".join(batch) + "\r\n").encode())
        except OSError as exc:
            raise SystemExit("device_check: cannot write QEAF.CMD (%s)" % exc)
        self.flush()
        deadline = time.time() + max(wait, 4.0)
        text = ""
        seq_before = self.seq
        while time.time() < deadline:
            time.sleep(0.6)
            text = self.response_text()
            # Two independent proofs: seq=N must move forward (this answer is
            # new) and the batch nonce must be echoed (it is our batch).
            match = re.search(r"(?m)^seq=(\d+)$", text)
            if match and int(match.group(1)) > seq_before and nonce in text:
                self.seq = int(match.group(1))
                self.last_response = text
                return text
        self.last_response = text
        if expect:
            raise SystemExit("device_check: no answer to %r in %.0fs:\n%s"
                             % (lines, wait, text[-400:]))
        return text

    def file(self, name):
        try:
            return Volume(self.image()).read(name) or b""
        except (OSError, ValueError):
            return b""

    # -- device actions ------------------------------------------------
    def flash(self):
        image = self.args.image
        if not os.path.exists(image):
            raise SystemExit("device_check: missing image %s" % image)
        size = os.path.getsize(image)
        print("flash: %s (%d bytes)" % (os.path.relpath(image, ROOT), size))
        if self.wait_ready(15) and self.probe(timeout=20.0):
            self.flash_rescue(image, size)
            return
        if self.letter:
            print("flash: QEAFRESCUE is mounted but silent - its worker "
                  "stopped answering, so the phone cannot flash itself")
        # Not answering: the phone may be sitting in the vendor OS with adb.
        subprocess.run(["adb", "reboot", "recovery"], capture_output=True)
        if self.wait_ready(120) and self.probe():
            print("flash: recovery answered after adb reboot")
            self.flash_rescue(image, size)
            return
        self.flash_fastboot(image)

    def flash_rescue(self, image, size):
        """Copy the image onto the RAM volume and let the phone flash itself.

        No fastboot: the flash verb walks BOOTIMG.BIN's cluster chain, so the
        copy Windows scatters over the drive is fine, and the phone verifies
        every byte against p77 before it answers."""
        target = os.path.join(self.letter + "\\", "BOOTIMG.BIN")
        print("flash: copying BOOTIMG.BIN to %s - the phone does the write"
              % self.letter)
        try:
            shutil.copyfile(image, target)
        except OSError as exc:
            raise SystemExit("flash: cannot write BOOTIMG.BIN (%s)" % exc)
        self.flush()
        staged = os.path.getsize(target)
        if staged != size:
            raise SystemExit("flash: BOOTIMG.BIN on the drive is %d bytes, "
                             "expected %d" % (staged, size))
        text = self.send(["flash recovery BOOTIMG.BIN confirm"], wait=420,
                         expect=True)
        for line in text.splitlines():
            if line.startswith("flash: "):
                print(line)
        if "written and verified byte for byte" not in text:
            raise SystemExit("flash: the rescue verb refused the image:\n%s"
                             % text[-400:])
        try:
            os.remove(target)
        except OSError:
            # The phone may drop the LUN as soon as it has written p77.
            pass
        try:
            self.flush()
        except OSError:
            pass
        self.reboot_recovery()

    def reboot_recovery(self):
        """Boot the image just written and wait for the fresh channel."""
        try:
            with open(os.path.join(self.letter + "\\", "QEAF.CMD"), "wb") as fh:
                fh.write(b"reboot recovery\r\n")
        except OSError as exc:
            raise SystemExit("flash: cannot ask for a reboot (%s)" % exc)
        self.flush()
        deadline = time.time() + 300
        while time.time() < deadline:
            time.sleep(3)
            text = self.response_text()
            if "seq=0" in text and "no command has run yet" in text:
                # A fresh boot restarts both counters, so must the host side.
                self.seq = 0
                self.frame = 0
                print("flash: rebooted into the new image, rescue answering")
                return
        raise SystemExit("flash: QEAFRESCUE did not come back after reboot")

    def flash_fastboot(self, image):
        print("flash: QEAFRESCUE is not answering - falling back to fastboot")
        deadline = time.time() + 60
        while time.time() < deadline:
            out = subprocess.run(["fastboot", "devices"], capture_output=True,
                                 text=True).stdout
            if out.strip():
                break
            time.sleep(2)
        else:
            raise SystemExit("flash: the phone is neither in QEAFRESCUE nor "
                             "in fastboot (hold Power+VolumeDown and retry)")
        done = subprocess.run(["fastboot", "flash", "recovery", image],
                              capture_output=True, text=True)
        tail = (done.stdout + done.stderr).strip().splitlines()[-3:]
        print("flash: " + " | ".join(tail))
        if done.returncode != 0 or "FAILED" in (done.stdout + done.stderr):
            raise SystemExit("flash: fastboot refused the write")
        subprocess.run(["fastboot", "reboot"], capture_output=True)
        if not self.wait_ready(120):
            raise SystemExit("flash: QEAFRESCUE never came back after fastboot")
        self.seq = 0
        self.frame = 0

    def snap(self, name):
        self.send(["snap"])
        raw = self.file("SCREEN.RAW")
        block = RAW_W * RAW_H * 2
        if len(raw) < block + 40:
            raise SystemExit("snap %s: SCREEN.RAW has %d bytes, expected %d"
                             % (name, len(raw), block + 40))
        pixels, trailer = raw[:block], raw[block:block + 40]
        frame = re.search(rb"frame=(\d+)", trailer)
        fnv = re.search(rb"fnv=([0-9a-f]{8})", trailer)
        if not frame or not fnv:
            raise SystemExit("snap %s: SCREEN.RAW carries no frame/fnv trailer"
                             % name)
        if fnv1a(pixels) != int(fnv.group(1), 16):
            raise SystemExit("snap %s: trailer hash does not match the pixels "
                             "(torn or stale capture)" % name)
        stage_m = re.search(rb"st=([0-9a-z?])", trailer)
        stage = STAGE_CODES.get(stage_m.group(1).decode(), "unknown") \
            if stage_m else "unmarked"
        dark = not any(pixels[i] | pixels[i + 1] for i in range(0, block, 2))
        if dark and stage in PRE_PAINT_STAGES | {"splash"}:
            raise SystemExit(
                "snap %s: black frame at stage '%s' - the splash had not been "
                "painted yet, so this capture says nothing about the renderer. "
                "Wait for 'ui: splash' / 'ui: LVGL phone shell active' in the "
                "boot journal, then snap again." % (name, stage))
        number = int(frame.group(1))
        if number <= self.frame:
            raise SystemExit("snap %s: frame %d is not newer than %d - the "
                             "device did not refresh SCREEN.RAW"
                             % (name, number, self.frame))
        self.frame = number
        os.makedirs(self.args.out, exist_ok=True)
        path = os.path.join(self.args.out, name + ".png")
        self.frames[name] = raw_to_png(raw, path)
        digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
        self.hashes[name] = digest
        print("snap  %-18s frame=%d %s stage=%-9s %s"
              % (name, number, digest[:12], stage,
                 os.path.relpath(path, ROOT)))
        return digest

    def difference(self, left, right):
        """Share of pixels that differ between two captures (0.0 .. 1.0)."""
        a, b = self.frames[left], self.frames[right]
        total = len(a) // 3
        changed = sum(1 for i in range(total) if a[i * 3:i * 3 + 3] != b[i * 3:i * 3 + 3])
        return changed / float(total)


# --------------------------------------------------------------------------
# Step language


def answered(text, prefix):
    """The phone's answer line for a verb, or a 'no answer' note."""
    for line in text.strip().splitlines():
        if line.startswith(prefix):
            return line
    return "no %s answer" % prefix.rstrip(":")


def run_steps(dev, steps):
    """Run the requested steps; returns a list of (ok, description)."""
    results = []

    def check(ok, text):
        results.append((ok, text))
        print("%s  %s" % ("ok  " if ok else "FAIL", text))

    for step in steps:
        words = step.split()
        if not words:
            continue
        verb = words[0].lower()
        if verb == "note":
            print("      -- " + " ".join(words[1:]))
        elif verb == "sleep":
            time.sleep(int(words[1]) / 1000.0)
        elif verb == "wake":
            if not dev.wait_ready(30):
                check(False, "wake: QEAFRESCUE is not up")
                continue
            dev.send(["power"])
            check(True, "wake: power-key press sent")
        elif verb == "unlock":
            dev.send(["unlock"])
            line = answered(dev.last_response, "unlock:")
            check(not line.startswith("no "), "unlock -> " + line)
        elif verb == "page":
            dev.send(["page " + words[1]])
            line = answered(dev.last_response, "page:")
            check("applied" in line or "queued" in line,
                  "page %s -> %s" % (words[1], line))
        elif verb == "tap":
            dev.send(["tap %s %s" % (words[1], words[2])])
            line = answered(dev.last_response, "tap:")
            check(not line.startswith("no "),
                  "tap %s %s -> %s" % (words[1], words[2], line))
        elif verb == "swipe":
            steps_n = words[5] if len(words) > 5 else "20"
            dev.send(["swipe %s %s %s %s %s"
                      % (words[1], words[2], words[3], words[4], steps_n)])
            line = answered(dev.last_response, "swipe:")
            check(not line.startswith("no "),
                  "swipe %s -> %s" % (" ".join(words[1:5]), line))
        elif verb == "key":
            dev.send(["power"])
            line = answered(dev.last_response, "power:")
            check(not line.startswith("no "), "key power -> " + line)
        elif verb == "cmd":
            dev.send([" ".join(words[1:])])
            check(True, "cmd  " + " ".join(words[1:]))
        elif verb == "snap":
            dev.snap(words[1])
        elif verb == "expect":
            what = words[1].lower()
            if what in ("same", "diff"):
                left, right = words[2], words[3]
                missing = [n for n in (left, right) if n not in dev.hashes]
                if missing:
                    check(False, "expect %s %s %s: no snapshot %s"
                          % (what, left, right, ", ".join(missing)))
                    continue
                # A pixel ratio, not a hash: the status-bar clock is part of
                # every frame and must not turn "the page did not change"
                # into a failure at a minute boundary.
                ratio = dev.difference(left, right)
                limit = 0.001 if what == "same" else 0.01
                ok = ratio <= limit if what == "same" else ratio >= limit
                check(ok, "expect %s %s %s (%.3f%% pixels differ)"
                      % (what, left, right, ratio * 100.0))
            elif what == "resp":
                text = " ".join(words[2:])
                check(text in dev.last_response,
                      "expect resp %r" % text)
            elif what == "file":
                name, text = words[2], " ".join(words[3:])
                check(text in dev.file(name).split(b"\x00")[0]
                      .decode("utf-8", "replace"), "expect file %s %r" % (name, text))
            else:
                check(False, "expect: unknown kind %r" % what)
        else:
            check(False, "unknown step %r" % step)
    return results


SCENARIOS = {
    # Wake, unlock, walk pages and prove each page really changed.
    "smoke": [
        "wake", "unlock",
        "snap home", "page settings", "snap settings",
        "expect diff home settings",
        "page files", "snap files",
        "expect diff settings files",
        "page home", "snap home-again",
        "expect same home home-again",
    ],
    # The power key's 2x2 menu: opened from the control center, and the
    # backdrop tap must close it without touching the page underneath.
    "power_menu": [
        "wake", "unlock",
        "swipe 360 40 360 900 20", "snap control",
        "swipe 360 1200 360 400 18",
        "swipe 360 1200 360 400 18",
        "swipe 360 1200 360 400 18",
        "snap control-bottom",
        "tap 640 1505", "snap power-menu",
        "expect diff control-bottom power-menu",
        "tap 185 300", "snap power-menu-closed",
        "expect same control-bottom power-menu-closed",
    ],
}


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--volume", default=None,
                    help="mounted QEAFRESCUE letter (default: auto-detect the "
                         "FAT volume holding SCREEN.RAW)")
    ap.add_argument("--image", default=IMAGE, help="boot image for --flash")
    ap.add_argument("--flash", action="store_true",
                    help="stage --image on the volume and let QEAFRESCUE write "
                         "p77 (fastboot only as a fallback)")
    ap.add_argument("--scenario", choices=sorted(SCENARIOS),
                    help="built-in step list")
    ap.add_argument("--steps", help="semicolon separated steps, see the docstring")
    ap.add_argument("--out", default=OUT_DIR, help="where the PNGs go")
    ap.add_argument("--no-wait", action="store_true",
                    help="skip waiting for the volume before starting")
    args = ap.parse_args()

    if not args.scenario and not args.steps and not args.flash:
        ap.error("give --scenario or --steps (or --flash)")

    dev = Device(args)
    started = time.time()
    if args.flash:
        dev.flash()
        if not args.steps and not args.scenario:
            # Flashing was the whole job (scripts/diagnose_cnss_70s.ps1 runs
            # the phone and collects the log itself).
            return 0
    elif not args.no_wait and not dev.wait_ready(30):
        wanted = ("QEAFRESCUE (%s)" % args.volume if args.volume else
                  "no mounted volume with SCREEN.RAW (QEAFRESCUE)")
        raise SystemExit("device_check: %s is not answering; "
                         "run --flash or plug the phone in" % wanted)

    steps = ([s.strip() for s in args.steps.split(";") if s.strip()]
             if args.steps else SCENARIOS[args.scenario])
    print("steps: %d  volume=%s" % (len(steps), dev.letter or "-"))
    results = run_steps(dev, steps)

    failed = [text for ok, text in results if not ok]
    print("\n%d checks, %d failed, %.0fs"
          % (len(results), len(failed), time.time() - started))
    if failed:
        for text in failed:
            print("  FAIL " + text)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
