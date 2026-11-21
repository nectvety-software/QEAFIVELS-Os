import os, shutil, socket, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
DIST = ROOT / "dist"
TEST_DISK = BUILD / "virtio-test.img"
AHCI_DISK = BUILD / "ahci-test.img"
QEMU = os.environ.get("VQEAF_QEMU") or shutil.which("qemu-system-x86_64") or r"C:\Program Files\QEMU\qemu-system-x86_64.exe"
SHARE = Path(r"C:\Program Files\QEMU\share")
CODE = Path(os.environ.get("VQEAF_OVMF_CODE", str(SHARE/"edk2-x86_64-code.fd")))
VARS_SOURCE = Path(os.environ.get("VQEAF_OVMF_VARS", str(SHARE/"edk2-i386-vars.fd")))

def boot(image, log, expected, settle=()):
    if not Path(QEMU).is_file() or not CODE.is_file() or not VARS_SOURCE.is_file():
        print("SKIP QEMU: qemu-system-x86_64 or OVMF firmware missing")
        return None
    if not TEST_DISK.is_file() or not AHCI_DISK.is_file():
        raise SystemExit("missing test disks; run scripts/make_test_disk.py first")
    vars_copy = BUILD / ("qemu-vars-" + log.stem + ".fd")
    shutil.copyfile(VARS_SOURCE, vars_copy)
    log.unlink(missing_ok=True)
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    command = [
        str(QEMU), "-machine", "q35", "-m", "512M", "-vga", "std",
        "-drive", f"if=pflash,format=raw,unit=0,readonly=on,file={CODE.as_posix()}",
        "-drive", f"if=pflash,format=raw,unit=1,file={vars_copy.as_posix()}",
        "-drive", f"file={(DIST/image).as_posix()},format=raw,if=ide,index=0",
        "-drive", f"file={TEST_DISK.as_posix()},format=raw,if=none,id=vqblk",
        "-device", "virtio-blk-pci,drive=vqblk,disable-modern=on,disable-legacy=off",
        "-drive", f"file={(BUILD/'ahci-test.img').as_posix()},format=raw,if=none,id=ahcidisk",
        "-device", "ide-hd,drive=ahcidisk,bus=ide.1",
        "-boot", "order=c", "-display", "none",
        "-serial", f"file:{log.as_posix()}",
        "-monitor", f"tcp:127.0.0.1:{port},server,nowait",
        "-no-reboot", "-no-shutdown"
    ]
    proc = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 20
        output = ""
        while time.monotonic() < deadline:
            if log.exists():
                output = log.read_text(errors="replace")
                if expected in output and all(m in output for m in settle):
                    time.sleep(0.15)
                    output = log.read_text(errors="replace")
                    break
            if proc.poll() is not None:
                break
            time.sleep(0.15)
        missing = [m for m in (expected, *settle) if m not in output]
        if missing:
            if proc.poll() is not None:
                print(proc.stderr.read().decode(errors="replace"))
            print(output)
            raise SystemExit("QEMU boot check failed; missing: " + ", ".join(missing))
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=2) as monitor:
                monitor.settimeout(1)
                try: monitor.recv(4096)
                except OSError: pass
                monitor.sendall(b"quit\n")
        except OSError:
            proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=3)
        print("QEMU PASS:", image, "->", expected)
        return output
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=3)
            except subprocess.TimeoutExpired: proc.kill()

def main():
    storage_done = ("STORAGE: phase 1+2 complete", "STORAGE: AHCI phase complete")
    normal = boot("vqeaf-x86_64-uefi.img", BUILD/"qemu-normal.log", "T1 alive", storage_done)
    if normal is not None:
        for marker in ("ExitBootServices SUCCESS", "GDT + IDT + exception handlers ready",
                       "physical page allocator", "VM: identity page tables active",
                       "kheap: online", "SCHED: tasks created with guard pages",
                       "DISPLAY: portrait 9:16", "GOP: framebuffer",
                       "UI: GOP framebuffer mapped",
                       "UI: native shell online",
                       "VIRTIO: controller online", "VIRTIO: PCI 1AF4:1001",
                       "VIRTIO: 2 in-flight async reads completed via INTx",
                       "VIRTIO: unsupported command returned device error",
                       "VIRTIO: async sector write + readback verified",
                       "STORAGE: FAT16 mounted read-only",
                       "STORAGE: I/O error tests passed",
                       "STORAGE: phase 1+2 complete",
                       "AHCI: controller online",
                       "STORAGE: AHCI disk mounted at /storage",
                       "STORAGE: /storage/AHCI.TXT -> VQEAF OS AHCI/SATA FAT16 read-only mount OK",
                       "STORAGE: AHCI scratch write + readback + restore verified",
                       "STORAGE: AHCI block registration is read-only (write rejected)",
                       "STORAGE: AHCI unmount/remount verified",
                       "STORAGE: AHCI phase complete",
                       "T1 alive", "T2 alive", "PF: guard page fault recovered",
                       "T3 recovered from guard page fault", "PIT timer IRQ0 enabled",
                       "PIT first tick observed"):
            if marker not in normal:
                raise SystemExit("QEMU kernel regression missing: " + marker)
    recovery = boot("vqeaf-x86_64-recovery.img", BUILD/"qemu-recovery.log",
                    "RECOVERY: menu ready (arrows + Enter)",
                    ("RECOVERY: GOP mode=", "portrait base=", "VQEAF Recovery 0.3"))
    if normal is None or recovery is None:
        print("QEMU execution not available; compile and host tests only.")
        return 2
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
