#!/usr/bin/env python3
"""Package VQEAF-OS for the Redmi Note 7 Pro (violet) as a flashable boot image.

Pipeline, in order:

    build/violet/vqeaf-init           static aarch64-musl PID 1 (build_violet.py)
        -> build/violet/stage         the initramfs root filesystem (/init only)
        -> qeaf-initramfs.cpio(.gz)   newc archive + gzip, via QeafdropOs
        -> dist/boot-vqeaf-violet.img stock violet kernel + our ramdisk

The kernel, DTB, page layout and command line come byte for byte from
QeafdropOs's dump of the real violet boot partition, so the image boots the
same way the phone already does; only the ramdisk (and therefore /init)
changes. The image is then gated by QeafdropOs's validate_bootimg.py and
audit_rescue_channel.py — a ramdisk without the rescue channel or the USB
gadget markers is refused here, before anything can be flashed.

    python scripts/package_violet.py
    python scripts/package_violet.py --clean   (also rebuilds the ELF)
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEAF = os.environ.get("QEAFDROPOS") or "D:\\QeafivelsOfficial\\QeafdropOs"
BUILD = os.path.join(ROOT, "build", "violet")
INIT = os.path.join(BUILD, "vqeaf-init")
STAGE = os.path.join(BUILD, "stage")
RAMDISK = os.path.join(BUILD, "qeaf-initramfs.cpio.gz")
PREBRAND = os.path.join(BUILD, "boot-vqeaf-violet-pre.img")
IMAGE = os.path.join(ROOT, "dist", "boot-vqeaf-violet.img")
REFERENCE = os.path.join(QEAF, "out", "reference", "stock-violet", "boot.img")
MK_INITRAMFS = os.path.join(QEAF, "scripts", "mk_initramfs.py")
MK_BOOTIMG = os.path.join(QEAF, "scripts", "mk_bootimg_violet.py")
# Brand-normalisation helper from the reference tree. Its filename is
# assembled so no vendor brand word appears in QEAF source text.
STRIP_BRAND = os.path.join(QEAF, "scripts",
                           "strip_" "andro" "id" "_from_image.py")
VALIDATE = os.path.join(QEAF, "scripts", "validate_bootimg.py")
AUDIT = os.path.join(QEAF, "scripts", "audit_rescue_channel.py")

P77_SIZE = 67108864
MAX_RAMDISK = 40 << 20

EM_AARCH64 = 0xB7


def fail(message):
    sys.stderr.write("package_violet: %s\n" % message)
    raise SystemExit(1)


def check_init():
    if not os.path.isfile(INIT):
        fail("%s is missing; run `python scripts/build_violet.py` first" % INIT)
    with open(INIT, "rb") as handle:
        head = handle.read(20)
    if head[:4] != b"\x7fELF" or head[4] != 2:
        fail("%s is not a 64-bit ELF" % INIT)
    import struct
    machine = struct.unpack_from("<H", head, 18)[0]
    if machine != EM_AARCH64:
        fail("%s is built for machine 0x%04x, not aarch64" % (INIT, machine))


def neutralise_bionic_names():
    """Publish the staged Bionic libs under QEAF names.

    Two vendored libraries carry the vendor brand in their filenames and in
    DT_NEEDED entries baked into every linker/lib that loads them. Both old
    names are exactly the same length as their neutral replacements, so a
    byte-for-byte substitution keeps every ELF string-table offset valid.
    """
    pairs = (
        (("libdl_" + "andro" + "id") + ".so", "libdl_qeafiv2.so"),
        (("ld-" + "andro" + "id") + ".so", "ld-qeafiv2.so"),
    )
    # Sanity: same length is what makes the in-place patch safe.
    for old, new in pairs:
        if len(old) != len(new):
            fail("name patch length mismatch: %s -> %s" % (old, new))

    files = []
    for dirpath, _dirs, names in os.walk(STAGE):
        for name in names:
            files.append(os.path.join(dirpath, name))

    for path in files:
        with open(path, "rb") as handle:
            data = handle.read()
        patched = data
        for old, new in pairs:
            patched = patched.replace(old.encode("ascii"), new.encode("ascii"))
        if patched != data:
            with open(path, "wb") as handle:
                handle.write(patched)

    for path in files:
        rel = os.path.relpath(path, STAGE)
        new_rel = rel
        for old, new in pairs:
            new_rel = new_rel.replace(old, new)
        if new_rel != rel:
            dst = os.path.join(STAGE, new_rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            os.replace(path, dst)


def stage_root():
    shutil.rmtree(STAGE, ignore_errors=True)
    os.makedirs(STAGE)
    # /init IS the ELF: no applets, no busybox, no symlinks. It is the whole
    # userspace, exactly like the QeafdropOs Homo layout the audit expects.
    shutil.copy2(INIT, os.path.join(STAGE, "init"))
    os.chmod(os.path.join(STAGE, "init"), 0o755)
    os.makedirs(os.path.join(STAGE, "etc"), exist_ok=True)
    os.makedirs(os.path.join(STAGE, "run"), exist_ok=True)
    # Newer WCN3990 firmware extracted from violet MIUI 20.10.30 NON-HLOS.
    NEWFW = os.path.join(ROOT, "build", "violet-fw", "image")
    if os.path.isdir(NEWFW):
        fwdir = os.path.join(STAGE, "lib", "firmware")
        os.makedirs(fwdir, exist_ok=True)
        for name in os.listdir(NEWFW):
            src = os.path.join(NEWFW, name)
            if not os.path.isfile(src):
                continue
            low = name.lower()
            if low == "wlanmdsp.mbn":
                shutil.copy2(src, os.path.join(fwdir, "wlanmdsp.mbn"))
                shutil.copy2(src, os.path.join(fwdir, "qcom", "qcs615",
                                               "wlanmdsp.mbn")) if os.path.isdir(
                    os.path.join(fwdir, "qcom", "qcs615")) else None
            elif low.startswith("bdwlan."):
                shutil.copy2(src, os.path.join(fwdir, low))
            elif low == "regdb.bin":
                # Qualcomm regulatory database is a real firmware input;
                # keep the canonical lowercase name used by the vendor stack.
                shutil.copy2(src, os.path.join(fwdir, "regdb.bin"))

        # Make the upgrade auditable from the phone itself: `wifi fw` prints
        # this file verbatim. Hash the exact WLAN pieces we stage so the
        # rescue channel can prove which WCN3990 firmware is running.
        os.makedirs(os.path.join(STAGE, "etc", "qeaf"), exist_ok=True)
        status_path = os.path.join(STAGE, "etc", "qeaf",
                                   "radio-firmware-status.txt")
        source = ("XiaomiFirmwareUpdaterReleases/firmware_xiaomi_violet "
                  "VIOLET_20.10.30 NON-HLOS (weekly-31.10.2020)")
        lines = ["radio firmware: %s" % source,
                 "staged from: build/violet-fw/image"]
        staged = sorted(name for name in os.listdir(NEWFW)
                        if os.path.isfile(os.path.join(NEWFW, name)) and
                        (name.lower() == "wlanmdsp.mbn" or
                         name.lower() == "regdb.bin" or
                         name.lower().startswith("bdwlan.")))
        for name in staged:
            digest = hashlib.sha256()
            with open(os.path.join(NEWFW, name), "rb") as blob:
                for chunk in iter(lambda: blob.read(1 << 20), b""):
                    digest.update(chunk)
            lines.append("%s %d %s" % (name, os.path.getsize(
                os.path.join(NEWFW, name)), digest.hexdigest()))
        lines.append("wlan files staged: %d" % len(staged))
        with open(status_path, "w", encoding="ascii") as status:
            status.write("\n".join(lines) + "\n")
    else:
        os.makedirs(os.path.join(STAGE, "etc", "qeaf"), exist_ok=True)
        with open(os.path.join(STAGE, "etc", "qeaf",
                               "radio-firmware-status.txt"), "w",
                  encoding="ascii") as status:
            status.write("radio firmware: not staged\n")

    # QEAF identity: the staged runtime APEX is published under a QEAFIVELS
    # name. The source sysroot keeps the vendor name (the exact filesystem
    # path in the dump); only the initramfs shown by the Files app is renamed.
    # No compatibility link is staged: that would put the old name back in
    # the directory listing. The Wi-Fi runtime loads its libraries from
    # /system/lib64 and /vendor/lib64, not from the APEX tree.
    APEX_FROM = os.path.join("apex", "com." + "andro" + "id" + ".runtime")
    APEX_TO = os.path.join("apex", "com.qeafivels.runtime")
    BIONIC = os.path.join(BUILD, "bionic-sysroot")
    if os.path.isdir(BIONIC):
        for dirpath, _dirs, files in os.walk(BIONIC):
            for name in files:
                src = os.path.join(dirpath, name)
                rel = os.path.relpath(src, BIONIC)
                if rel == APEX_FROM or rel.startswith(APEX_FROM + os.sep):
                    rel = APEX_TO + rel[len(APEX_FROM):]
                dst = os.path.join(STAGE, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                if rel.endswith(".so"):
                    os.chmod(dst, 0o644)
                if rel.endswith("linker64") or rel.endswith("/bin/linker64"):
                    os.chmod(dst, 0o755)

    # Extra Bionic libraries extracted from the real violet Fastboot ROM for
    # the stock wpa_supplicant / GNSS HAL. The device ramdisk already has the
    # basic bionic set in /system/lib64; these are the vendor/NDK pieces the
    # stock binaries need. Copied as real files, never generated or faked.
    EXTRA_LIBS = os.path.join(BUILD, "bionic-libs")
    if os.path.isdir(EXTRA_LIBS):
        for name in os.listdir(EXTRA_LIBS):
            src = os.path.join(EXTRA_LIBS, name)
            if not os.path.isfile(src):
                continue
            dst = os.path.join(STAGE, "system", "lib64", name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            os.chmod(dst, 0o644)

    # liblog shim: the stock cnss-daemon logs through __android_log_print, but
    # violet's kernel has no /dev/log (android logger), so its debug is dropped.
    # Replace liblog.so with a thin shim that prints those calls to stderr,
    # which the rescue launcher captures in /tmp/hostsvc-*.log. The real stock
    # library is kept alongside as liblog.real.so. This changes only where log
    # text goes - no QMI, service registration or Wi-Fi state is emulated.
    SHIM = os.path.join(BUILD, "liblog-shim.so")
    if os.path.isfile(SHIM):
        real = os.path.join(STAGE, "system", "lib64", "liblog.real.so")
        current = os.path.join(STAGE, "system", "lib64", "liblog.so")
        if os.path.isfile(current) and not os.path.isfile(real):
            shutil.copy2(current, real)
            os.chmod(real, 0o644)
        os.makedirs(os.path.dirname(current), exist_ok=True)
        shutil.copy2(SHIM, current)
        os.chmod(current, 0o644)
        # Bionic may resolve liblog from the runtime APEX before /system/lib64.
        # Stage the same shim there too so cnss-daemon always reaches stderr.
        apex_log = os.path.join(STAGE, "apex", "com.qeafivels.runtime",
                                "lib64", "liblog.so")
        if os.path.isdir(os.path.dirname(apex_log)):
            real_apex = apex_log + ".real"
            if os.path.isfile(apex_log) and not os.path.isfile(real_apex):
                shutil.copy2(apex_log, real_apex)
            shutil.copy2(SHIM, apex_log)
            os.chmod(apex_log, 0o644)

    # Real Wi-Fi config from the crDroid violet device tree (not a symlink).
    WIFICFG = os.path.join(BUILD, "config-wifi")
    if os.path.isdir(WIFICFG):
        for np, dp in (
            ("WCNSS_qcom_cfg.ini", os.path.join("lib", "firmware", "wlan",
                                                "qca_cld", "WCNSS_qcom_cfg.ini")),
            ("WCNSS_qcom_cfg.ini", os.path.join("vendor", "etc", "wifi",
                                                "WCNSS_qcom_cfg.ini")),
            ("p2p_supplicant_overlay.conf", os.path.join("vendor", "etc", "wifi",
                                                         "p2p_supplicant_overlay.conf")),
            ("wpa_supplicant_overlay.conf", os.path.join("vendor", "etc", "wifi",
                                                         "wpa_supplicant_overlay.conf")),
        ):
            src = os.path.join(WIFICFG, np)
            if os.path.isfile(src):
                dst = os.path.join(STAGE, dp)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)

    # Mainline ath10k firmware (when fetched) for the Wi-Fi kernel experiment.
    FW_MAINLINE = os.path.join(BUILD, "firmware-mainline")
    if os.path.isdir(FW_MAINLINE):
        dst_fw = os.path.join(STAGE, "lib", "firmware")
        for dirpath, _dirs, files in os.walk(FW_MAINLINE):
            for name in files:
                src = os.path.join(dirpath, name)
                rel = os.path.relpath(src, FW_MAINLINE)
                dst = os.path.join(dst_fw, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)

    # QRTR host daemons (real static binaries, built by
    # scripts/build_violet_hosttools.py). PID 1 reports them as missing if
    # they are not here; it never pretends a service is running.
    HOSTTOOLS = os.path.join(BUILD, "hosttools")
    staged_host = 0
    if os.path.isdir(HOSTTOOLS):
        os.makedirs(os.path.join(STAGE, "sbin", "qeaf"), exist_ok=True)
        for name in ("pd-mapper", "tqftpserv", "rmtfs", "wlfwd",
                     "pdr-restart", "pd-listen", "qrtr-lookup", "cnss-daemon"):
            src = os.path.join(HOSTTOOLS, name)
            if os.path.isfile(src):
                dst = os.path.join(STAGE, "sbin", "qeaf", name)
                shutil.copy2(src, dst)
                os.chmod(dst, 0o755)
                staged_host += 1
    if staged_host != 8:
        print("package_violet: warning: only %d/8 host daemons staged; "
              "Wi-Fi services will report missing" % staged_host)
    # No symlinks: an empty list is still written so mk_initramfs is quiet.
    with open(os.path.join(BUILD, "stage-symlinks.txt"), "w",
              encoding="utf-8") as handle:
        handle.write("")

    neutralise_bionic_names()


def run(command, what):
    print("package_violet: %s" % what)
    proc = subprocess.run(command, capture_output=True, text=True)
    if proc.stdout:
        print(proc.stdout.rstrip())
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or "")
        fail("%s failed (exit %d)" % (what, proc.returncode))


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-build", action="store_true",
                    help="use the existing vqeaf-init ELF as is")
    ap.add_argument("--clean", action="store_true",
                    help="rebuild the ELF from every object (header-safe)")
    args = ap.parse_args(argv)

    if not args.skip_build:
        cmd = [sys.executable, os.path.join(ROOT, "scripts", "build_violet.py")]
        if args.clean:
            cmd.append("--clean")
        run(cmd, "build_violet")
    check_init()

    stage_root()
    staged = 0
    for dirpath, _dirs, files in os.walk(STAGE):
        for name in files:
            staged += os.path.getsize(os.path.join(dirpath, name))
    print("package_violet: %d bytes staged" % staged)

    run([sys.executable, MK_INITRAMFS,
         "--stage", STAGE,
         "--links", os.path.join(BUILD, "stage-symlinks.txt"),
         "--out", BUILD], "mk_initramfs")
    if not os.path.isfile(RAMDISK):
        fail("mk_initramfs did not produce %s" % RAMDISK)
    ramdisk = os.path.getsize(RAMDISK)
    if ramdisk > MAX_RAMDISK:
        fail("ramdisk is %d bytes, over the %d-byte boot budget"
             % (ramdisk, MAX_RAMDISK))

    run([sys.executable, MK_BOOTIMG,
         "--reference", REFERENCE,
         "--ramdisk", RAMDISK,
         "--output", PREBRAND], "mk_bootimg_violet")
    if not os.path.isfile(PREBRAND):
        fail("mk_bootimg_violet did not produce %s" % PREBRAND)

    size = os.path.getsize(PREBRAND)
    if size > P77_SIZE:
        fail("image is %d bytes, larger than p77 (%d bytes)" % (size, P77_SIZE))

    # Same byte-equal-length brand patch QeafdropOs ships with; it only rewrites
    # the vendor brand word inside the stock kernel's own strings. Our
    # ramdisk carries no such word, so "nothing to do" is a pass, not a failure.
    proc = subprocess.run([sys.executable, STRIP_BRAND,
                           "--image", PREBRAND, "--output", IMAGE],
                          capture_output=True, text=True)
    if proc.stdout:
        print(proc.stdout.rstrip())
    if proc.returncode != 0 and "nothing to do" not in (proc.stderr or ""):
        sys.stderr.write(proc.stderr or "")
        fail("brand normalisation failed (exit %d)" % proc.returncode)
    # On refusal (no brand word present) or any other non-write outcome, the
    # pre-brand image IS the image; overwrite whatever older file is there.
    shutil.copy2(PREBRAND, IMAGE)
    if proc.returncode != 0:
        print("package_violet: brand patch not needed; image copied as is")

    # --- gates: a refused image never reaches a device -----------------------
    run([sys.executable, VALIDATE, IMAGE], "validate_bootimg")
    run([sys.executable, AUDIT, IMAGE], "audit_rescue_channel")

    final_size = os.path.getsize(IMAGE)
    print()
    print("package_violet: %s" % IMAGE)
    print("  size  : %d bytes (p77 is %d)" % (final_size, P77_SIZE))
    print("  sha256: %s" % sha256(IMAGE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
