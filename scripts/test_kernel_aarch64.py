#!/usr/bin/env python3
"""Cumulative gate for the independent AArch64 kernel on QEMU virt (A-F3).

EMULATED evidence (qemu-system-aarch64 -machine virt -cpu cortex-a57), NOT
physical violet hardware. Builds the kernel, boots it with -kernel, asserts the
PL011 UART markers and a clean PSCI SYSTEM_OFF exit:
  A boot/EL1/PSCI, B MMU/TTBR0/PMM, C exceptions/GIC/timer/SVC,
  D preemptive scheduler + EL0 task, E portable ELF loader running an EL0 image,
  F1 Binder-like kernel IPC, F2a .qen loader + R_AARCH64_RELATIVE at two bases,
  F2b Launcher/AMS spawning an isolated EL0 app process at run time,
  F2c legacy EL0 surfaces -> kernel blit -> ramfb framebuffer,
  F3 real window/layer compositor + PL011 RX keyboard input + EL0 SystemUI
     launcher connected to the surface path (SurfaceFlinger + InputFlinger +
     SystemUI chain), verified by real key injection and pixel readback,
  F4 per-task uid/gid/capability model, capability-gated EL0 syscall dispatch and
     per-task VFS handle tables with owner/mode checks (denials asserted).

Keyboard input is REAL: the serial console is a bidirectional TCP chardev. The
host connects, waits for the guest's own markers, then sends 'd', Enter and 'x'
into the PL011 RX FIFO. The GIC delivers INTID 33 and the kernel routes each
byte to the focused layer. No input is fabricated inside the guest.
"""
import importlib.util
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "kernel-aarch64"
QEMU = os.environ.get("VQEAF_QEMU_AARCH64") or \
    r"C:\Program Files\QEMU\qemu-system-aarch64.exe"

# marker seen -> (bytes to inject, one-shot tag)
KEY_STEPS = (
    (b"LAUNCHER EL0: home ready", b"d", "focus-right"),
    (b"LAUNCHER EL0: focus=1", b"\n", "enter"),
    (b"APP WINDOW: layer up", b"x", "app-key"),
)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


builder = load("build_kernel_aarch64", ROOT / "scripts" / "build_kernel_aarch64.py")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def boot_with_keyboard(elf, log_path, disk_path, extra_args=()):
    """Run QEMU with a bidirectional serial chardev and inject real keys."""
    port = free_port()
    cmd = [QEMU, "-machine", "virt", "-cpu", "cortex-a57", "-m", "128M",
           "-display", "none", "-monitor", "none", "-no-reboot",
           "-device", "ramfb",
           "-drive", "file=%s,if=none,id=blk0,format=raw" % disk_path,
           "-device", "virtio-blk-device,drive=blk0",
           "-chardev",
           "socket,id=vq,host=127.0.0.1,port=%d,server=on,wait=on" % port,
           "-serial", "chardev:vq", "-kernel", str(elf)] + list(extra_args)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, encoding="utf-8", errors="replace")
    conn = None
    for _ in range(150):
        try:
            conn = socket.create_connection(("127.0.0.1", port), timeout=1)
            break
        except OSError:
            if proc.poll() is not None:
                break
            time.sleep(0.1)
    if conn is None:
        out, err = proc.communicate(timeout=5)
        return "", proc.returncode, err, []
    conn.settimeout(0.2)
    buf = bytearray()
    sent = []
    deadline = time.monotonic() + 60.0
    while time.monotonic() < deadline:
        try:
            data = conn.recv(65536)
            if data:
                buf.extend(data)
            elif proc.poll() is not None:
                break
        except socket.timeout:
            if proc.poll() is not None:
                break
        except OSError:
            break
        for marker, key, tag in KEY_STEPS:
            if marker in buf and tag not in sent:
                try:
                    conn.sendall(key)
                    sent.append(tag)
                except OSError:
                    pass
    try:
        conn.close()
    except OSError:
        pass
    try:
        out, err = proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
    text = buf.decode("utf-8", "replace")
    log_path.write_text(text, encoding="utf-8")
    return text, proc.returncode, err, sent


def main():
    if not Path(QEMU).is_file():
        raise SystemExit("aarch64 kernel gate: qemu-system-aarch64 not found at %s "
                         "(set VQEAF_QEMU_AARCH64)" % QEMU)
    elf = builder.build()
    print("kernel ELF: %s (%d bytes)" % (elf.as_posix(), elf.stat().st_size))
    # Scratch raw disk for the G1 virtio-mmio block driver (1 MiB = 2048 sectors).
    # The driver's self-test writes/reads the last sector; this image is throwaway.
    disk = OUT / "g1-scratch.img"
    disk.write_bytes(b"\x00" * (1024 * 1024))
    log = OUT / "uart-boot.log"
    # QEMU's DEFAULT virtio-mmio transport is transitional (Version reads 1 but
    # the modern register block answers). Force a clean modern v2 transport for
    # the main run so the gate exercises the descriptor-ring path explicitly.
    out, rc, err, injected = boot_with_keyboard(
        elf, log, disk.as_posix(),
        ("-global", "virtio-mmio.force-legacy=false"))
    print("== QEMU virt AArch64 kernel UART (bidirectional serial) ==")
    print(out, end="")
    if err:
        print(err, end="", file=sys.stderr)

    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + msg)
        ok = ok and cond

    # ---- Milestones A-C: boot, MMU, exceptions ---------------------------
    check("VQEAF kernel aarch64 0.1: native entry reached" in out, "kernel entry reached")
    check("AARCH64: CurrentEL=EL1" in out, "running at EL1")
    check("AARCH64: RAM probe write/read OK" in out, "RAM write/read verified")
    check("PMM: free pages=" in out, "physical page allocator online")
    check("MMU: identity page tables active; TTBR0=" in out, "identity page tables active")
    check("MMU: SCTLR.M=1 confirmed" in out, "MMU enabled (SCTLR.M=1)")
    check("MMU: post-enable UART OK" in out, "device mapping works after MMU enable")
    check("MMU: non-identity VA translation OK" in out, "non-identity VA translation verified")
    check("EXC: vectors installed" in out, "exception vector table installed")
    check("GIC: distributor + CPU interface online" in out, "GICv2 online")
    check("TIMER: tick 1" in out and "TIMER: tick 2" in out and "TIMER: tick 3" in out,
          "generic timer delivered 3 IRQ ticks")
    check("TIMER: 3 ticks observed" in out, "timer tick count reached main loop")
    check("EXC: SVC decoded (EC=0x15)" in out, "SVC sync exception decoded (EC=0x15)")
    check("EXC: returned from SVC" in out, "eret returned past SVC")
    check("AARCH64 KERNEL: boot OK" in out, "kernel boot completed")

    # ---- Milestone D/E/F1/F2a/F2b (regression) ---------------------------
    check("SCHED: tasks created (kernel=4, user=8)" in out,
          "scheduler created 4 kernel + 8 static EL0 tasks")
    check("SCHED: starting scheduler" in out, "scheduler started")
    check("SCHED: entering first task" in out, "entered first task via eret")
    check("SCHED: T1 alive" in out, "kernel task T1 ran")
    check("SCHED: T2 alive" in out, "kernel task T2 ran")
    check("SCHED: switch to EL0 task" in out, "switched into an EL0 user task")
    check("USER: EL0 reached" in out, "EL0 user stub executed and printed via SVC")
    check("ELF: user program loaded; entry=0x" in out, "static AArch64 ELF loaded into user region")
    check("USER ELF: AArch64 image loaded and running" in out, "ELF-loaded EL0 program ran via SVC")
    check("IPC: sender + receiver EL0 tasks created" in out, "IPC sender + receiver EL0 tasks created")
    check("IPC: kernel accepted send" in out, "kernel validated + accepted sys_send from EL0")
    check("IPC: kernel delivered recv" in out, "kernel delivered sys_recv to a different EL0 task")
    check("IPC: hello via kernel channel" in out, "message traversed sender -> kernel -> receiver")
    check("QEN: module loaded at base1=" in out, ".qen module loaded into isolated user roots")
    check("QEN: R_AARCH64_RELATIVE applied; two isolated EL0 instances" in out,
          "loader applied R_AARCH64_RELATIVE fixups by load base")
    check(out.count("QEN: relocated arm64 module running at EL0") >= 2,
          "relocated .qen ran correctly at BOTH static load bases (relocation proven)")
    check("LAUNCHER: home ready" in out, "kernel Launcher reached home screen")
    check("LAUNCHER: installed com.vqeaf.demo" in out, "Launcher listed the installed .qen app")
    check("AMS: launch com.vqeaf.demo -> pid=" in out, "AMS loaded + spawned the app as an EL0 process")
    check("LAUNCHER: spawned app pid=" in out, "Launcher confirmed the spawned app pid")
    check("AMS: process pid=" in out and " exited" in out, "AMS recorded the app process exit (lifecycle)")
    check(out.count("QEN: relocated arm64 module running at EL0") == 3,
          "the AMS-launched app ran at EL0 (3rd relocated instance)")

    # ---- Milestone F4: credentials, capability gate, VFS handles ---------
    check("SEC: credential model online (uid/gid/caps per task)" in out,
          "per-task uid/gid/capability model online")
    check("SEC: kernel cred uid=0 caps=0x00000000000007ff" in out,
          "kernel task holds the system identity with VQEAF_CAP_ALL")
    check("SEC: allow uid=0 sys=8 need=0x0000000000000020" in out,
          "capability policy allows CAP_WINDOW for the system identity")
    check("SEC: deny uid=10002 sys=8 need=0x0000000000000020" in out,
          "capability policy denies window create to a headless app")
    check("SEC: deny uid=10002 sys=6 need=0x0000000000000008" in out,
          "capability policy denies surface submit to a headless app")
    check("SEC: allow uid=10002 sys=2 need=0x0000000000000001" in out,
          "capability policy still allows console output")
    check("SEC: deny uid=10002 sys=16 need=0x0000000000000100" in out,
          "capability policy denies file writes without CAP_FS_WRITE")
    check("SEC: system uid rejected for user task" in out,
          "user task creation refuses the system uid")
    check("SEC: FS_ADMIN capability rejected for user task" in out,
          "user task creation refuses the FS_ADMIN bypass")
    check("SEC: unknown slot has no credential (fail closed)" in out,
          "a slot without a credential exposes no identity to the dispatcher")
    check("SEC: user task create with system cred -> refused -6" in out,
          "task creation rejects a privileged credential before allocating a slot")
    check("SEC EL0: sys_open denied -EACCES" in out,
          "real EL0 SVC: file open denied by the capability gate")
    check("SEC EL0: sys_write denied -EACCES" in out,
          "real EL0 SVC: file write denied by the capability gate")
    check("VFS: ramfs root online" in out, "kernel VFS root mounted")
    check("VFS: uid=10001 open own private -> h=0" in out,
          "app opened its own 0600 RAM file")
    check("VFS: uid=10001 write 5 bytes -> 5" in out, "app wrote 5 bytes to its own file")
    check("VFS: uid=10001 read 5 bytes -> 5 bytes" in out,
          "app read the same 5 bytes back through a fresh handle")
    check("VFS: uid=10002 open /data/10001/private -> EACCES" in out,
          "cross-app open refused by vnode owner/mode, not by path text")
    check("VFS: uid=10001 read /system/version -> 8 bytes" in out,
          "world-readable system file readable by an app")
    check("VFS: uid=10001 write /system/version -> EACCES" in out,
          "read-only system file refuses an app write")
    check("VFS: per-task handle tables isolated slot0 h=0 slot1 h=0" in out,
          "handle numbers are per task slot, never shared")
    check("VFS: foreign cred on bound handle -> EACCES" in out,
          "a handle cannot be used with another app's credential")
    check("VFS: task exit released slot handles -> h=0" in out,
          "task release frees its handle table")
    check(re.search(r"AMS: launch com\.vqeaf\.window -> pid=\d+ uid=10002 caps=0x", out)
          is not None,
          "AMS spawned the window app with its own uid and restricted capability mask")
    check("APP WINDOW: layer up" in out and "APP WINDOW: key=x" in out,
          "the restricted app still exercised its permitted window/input syscalls")

    # ---- Milestone F2c (legacy surface regression) -----------------------
    check("DISPLAY: ramfb programmed 320x240" in out, "ramfb framebuffer programmed via fw_cfg DMA")
    check("COMPOSITOR: client + compositor tasks created" in out, "compositor client + task created")
    check(out.count("COMPOSITOR: kernel accepted surface") == 2,
          "kernel validated + accepted 2 EL0 surface submissions")
    check("COMPOSITOR: composed 2 surfaces" in out, "compositor blitted both legacy surfaces")
    check("COMPOSITOR: pixel(4,4)=0x00000000ffff0000" in out, "readback: red surface pixel at (4,4)")
    check("COMPOSITOR: pixel(20,4)=0x00000000ff00c8c8" in out, "readback: teal surface pixel at (20,4)")
    check("COMPOSITOR: pixel(40,40)=0x00000000ff101010" in out, "readback: untouched background pixel")
    bg, red, teal = 0xFF101010, 0xFFFF0000, 0xFF00C8C8
    expect_sum = ((320 * 240 - 128) * bg + 64 * red + 64 * teal) & 0xFFFFFFFF
    check(("COMPOSITOR: checksum=0x%016x" % expect_sum) in out,
          "readback: legacy-phase checksum matches background + 2 surfaces")
    check("COMPOSITOR: frame ready" in out, "legacy compositor finished its frame")

    # ---- Milestone G1: virtio-mmio block driver (v0.2 driver model) --------
    # QEMU hands virtio-mmio devices out from the last transport backwards, so a
    # single disk lands at 0x0a000000 + 31*0x200. Assert the region, not the slot.
    check(re.search(r"VMMIO: block transport base=0x0{8}0a00[0-9a-f]{4}", out)
          is not None, "virtio-mmio block transport found in 0x0a000000 region")
    check("version=2 capacity=2048 sectors queue=256" in out,
          "modern v2 transport, 2048-sector capacity, 256-desc queue")
    check("VMMIO: probe READY instance=0 block id=0" in out,
          "driver probe READY + block device registered (v0.2 model)")
    check("VMMIO: scratch sector write/read self-test PASS" in out,
          "guest scratch-sector write/read self-test passed")
    check("G1: virtio-mmio storage online" in out, "G1 storage bring-up completed")
    check("VMMIO: probe FAILED" not in out and "VMMIO: region map FAILED" not in out,
          "no virtio-mmio probe/map failure")
    # Host-side proof the write reached the real disk image (not guest-internal):
    # the last sector must hold the driver's pattern byte[i] = (i*7+13) & 0xFF.
    last = disk.read_bytes()[-512:]
    expect = bytes(((i * 7 + 13) & 0xFF) for i in range(512))
    check(last == expect,
          "host readback: last sector of disk image matches written pattern")

    # ---- G1b: legacy v1 transport regression -----------------------------
    # QEMU's DEFAULT virtio-mmio transport is transitional: the Version register
    # reads 1 but the modern register block already answers (QueueNumMax at
    # 0x034). To exercise the genuine legacy PFN path (Version=1, QueueNumMax at
    # 0x020, QueuePFN at 0x02c) we must force legacy explicitly.
    disk.write_bytes(b"\x00" * (1024 * 1024))
    legacy_log = OUT / "uart-legacy.log"
    legacy_out, legacy_rc, legacy_err, legacy_injected = boot_with_keyboard(
        elf, legacy_log, disk.as_posix(),
        ("-global", "virtio-mmio.force-legacy=true"))
    if legacy_err:
        print(legacy_err, end="", file=sys.stderr)
    check(re.search(r"VMMIO: block transport base=0x0{8}0a00[0-9a-f]{4}", legacy_out)
          is not None, "G1b: legacy v1 block transport found in 0x0a000000 region")
    check("version=1 capacity=2048 sectors queue=256" in legacy_out,
          "G1b: legacy v1 transport negotiated (PFN queue)")
    check("VMMIO: scratch sector write/read self-test PASS" in legacy_out,
          "G1b: legacy scratch sector write/read self-test passed")
    check("VMMIO: probe FAILED" not in legacy_out,
          "G1b: no legacy virtio-mmio probe failure")
    legacy_last = disk.read_bytes()[-512:]
    check(legacy_last == expect,
          "G1b: host readback after legacy boot matches written pattern")
    check(len(legacy_injected) == 3,
          "G1b: legacy boot consumed all 3 injected keystrokes")
    check(legacy_rc == 0, "G1b: legacy QEMU exited cleanly (rc=%s)" % legacy_rc)

    # ---- Milestone F3: real layers + keyboard input + EL0 SystemUI -------
    check("WM: window manager online (max 8 layers)" in out, "window manager initialised")
    check("INPUT: PL011 RX IRQ armed (GIC INTID 33)" in out, "PL011 RX interrupt armed on GIC INTID 33")
    check("LAUNCHER EL0: SystemUI process created" in out, "EL0 SystemUI launcher process created")
    check("WM: create win=1 owner=" in out and "bounds=48,40,200x160" in out,
          "EL0 launcher created a real window (48,40,200x160)")
    check("WM: focus -> win 1 (z=0)" in out, "focus stack selected the launcher layer")
    check("LAUNCHER EL0: home ready" in out, "EL0 launcher drew and presented its home layer")
    check("WM: present win=1" in out, "launcher submitted its buffer through present()")
    check("LAUNCHER EL0: key=d" in out, "keyboard byte 'd' arrived via PL011 RX IRQ at EL0")
    check("LAUNCHER EL0: focus=1" in out, "SystemUI moved app focus and redrew the layer")
    # Assert the invariant, not a hardcoded pid. Whichever app AMS launches
    # first owns pid 1; that ordering used to depend on a scheduler race, so
    # pinning "-> pid=1" was both brittle and an artefact of the old bug.
    win_ams = re.search(r"AMS: launch com\.vqeaf\.window -> pid=(\d+)", out)
    win_sys = re.search(r"SYSCALL: EL0 launch request com\.vqeaf\.window -> pid=(\d+)", out)
    demo_ams = re.search(r"AMS: launch com\.vqeaf\.demo -> pid=(\d+)", out)
    check(win_sys is not None, "EL0 launcher asked AMS to launch an app via sys_launch")
    check(win_ams is not None, "AMS spawned the window app as a new isolated EL0 process")
    check(win_ams is not None and win_sys is not None
          and win_ams.group(1) == win_sys.group(1),
          "sys_launch returned exactly the pid AMS assigned to the window app")
    check(win_ams is not None and demo_ams is not None
          and win_ams.group(1) != demo_ams.group(1),
          "AMS assigned distinct pids to the two launched EL0 processes")
    check("WM: create win=2 owner=" in out and "z=1 bounds=96,80,128x96" in out,
          "AMS-spawned EL0 app created its own z=1 layer over the launcher")
    check("WM: focus -> win 2 (z=1)" in out, "focus moved to the top (z=1) layer")
    check("APP WINDOW: layer up" in out, "app layer drew and presented through the surface path")
    check("APP WINDOW: key=x" in out, "post-launch key routed to the focused z=1 app layer")
    check("LAUNCHER EL0: key=x" not in out,
          "InputFlinger did NOT leak the app's key to the unfocused launcher")
    m = re.search(r"COMPOSITOR: f3 frame (\d+) composed \(windows=2, focus=2, deliveries=3, input_irqs=(\d+)\)", out)
    check(m is not None, "compositor composed a frame with 2 layers, focus=2, 3 key deliveries")
    check(m is not None and int(m.group(2)) >= 3,
          "PL011 RX IRQ actually fired (input_irqs >= 3, not a fake event)")
    check("COMPOSITOR: f3 pixel(70,80)=0x00000000ff35e0c0" in out,
          "readback: launcher tile visible outside the app window")
    check("COMPOSITOR: f3 pixel(100,100)=0x00000000ff202a40" in out,
          "readback: z=1 app layer correctly occludes the z=0 launcher layer")
    check("COMPOSITOR: f3 pixel(4,4)=0x00000000ffff0000" in out,
          "readback: legacy red surface survived the full recompose")
    check("COMPOSITOR: f3 pixel(20,4)=0x00000000ff00c8c8" in out,
          "readback: legacy teal surface survived the full recompose")
    check("COMPOSITOR: f3 pixel(300,220)=0x00000000ff101010" in out,
          "readback: layer clipping left outside pixels as background")
    check("COMPOSITOR: f3 frame ready" in out, "F3 compositor emitted the composed frame")
    check("COMPOSITOR: f3 TIMEOUT" not in out, "F3 evidence produced before the deadline")
    check("WM: close win=1" in out and "WM: close win=2" in out,
          "windows were closed when their EL0 owner processes exited")
    check("APP WINDOW: closing" in out, "app layer lifecycle completed")
    check("SCHED: all tasks completed (exits=14" in out, "all 14 tasks exited cleanly")
    check("SCHED: warning: task counters drifted" not in out,
          "task counters stayed consistent (no task-creation race)")
    check("SCHED: no runnable task" not in out,
          "scheduler never stalled with no runnable task")
    check("SCHED: context switches occurred" in out, "preemptive context switches occurred")
    check(len(injected) == 3, "host injected all 3 real keystrokes over the serial console")
    check(rc == 0, "QEMU exited cleanly via PSCI SYSTEM_OFF (rc=%s)" % rc)

    print("test_kernel_aarch64: %s" % ("ALL OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
