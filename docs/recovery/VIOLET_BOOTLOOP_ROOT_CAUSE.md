# Violet Bootloop Root Cause — MRE Image Flashed to p77

Date: 2026-11-23
Devices/artifacts: Redmi Note 7 Pro "violet" (SM6150, 6 GB), QEAFIVELS OS 0.3.0, MRE OS bring-up kernel.

## 1. Symptoms (actual)

- After flashing a candidate MRE `BOOTIMG.BIN` to p77 through the QEAF rescue verb
  `flash recovery BOOTIMG.BIN confirm` and issuing `reboot recovery`, the handset
  rebooted endlessly (user-visible: stuck at the boot logo / repeated resets).
- The phone's USB gadget during the loop never exposed the QEAF rescue LUN.
- After `fastboot reboot`, the phone booted a stock kernel whose gadget is
  `Linux File-Stor Gadget REV_0310` with an OPPO DRIVER CDROM and a zero-sector
  mass-storage LUN — the vendor/stock boot, not QEAFIVELS (whose gadget is
  `REV_0414`, serial `vqeaf-os-violet`, mass-storage only).

## 2. Collected evidence

- QEAF rescue channel handshakes (RESPONSE.TXT seq=1..5, echo nonce round-trip).
- `status`/`diag` output from the last healthy QEAF boot (uptime 8215 s, boots 705).
- `wifi cat /proc/meminfo` → MemTotal 5,741,068 kB → 6 GB LPDDR4X variant.
- DTB inspection: live ABL tree (`violet-abl.dtb`) has root compatible
  `xiaomi,violet, qcom,sm6150`, GICv3 0x17A00000/0x17A60000, APSS watchdog
  `qcom,apss-wdt-qcs615` @ 0x17C10000, `arm,armv8-timer`, and the debug console
  `serial@880000` compatible `qcom,geni-debug-uart`.
- QEAFIVELS source: `platform/violet/boot_journal.c` — the boot journal lives in
  the tail of p77 (`VIO_JOURNAL_OFFSET = 64 MiB - 16 KiB`); QEAFIVELS OS 0.3.0
  runs from the p77 boot image; `reboot recovery` (`rescue.c`, `LINUX_REBOOT_CMD_RESTART2`)
  makes ABL boot p77 and the restart reason persists across watchdog resets.
- Verified backup: `D:\QEAFIVELS-Os\out\p77-backup-20261114-061514.img`
  (67,108,864 bytes = full p77, valid `ANDROID!` boot image,
  sha256 `5dd899f8013d1df696c6862dc543c95a973585f92b05c2c990ed684832f76637`).
- MRE kernel source audit (`kernel/arch/aarch64/boot/entry.S`, `kernel/core/violet.c`).

## 3. Last working boot stage

QEAFIVELS OS 0.3.0 PID 1 was fully up (rescue LUN bound at 0.010 s, stage `loop`,
UI active) right until the MRE image replaced the p77 boot payload.

## 4. Root cause (verified)

The bootloop is the compound result of three verified facts:

1. **MRE entry ABI bug** — `entry.S` copied **x2** into the DTB register while the
   ARM64 Image boot protocol passes the FDT physical address in **x0**. On the
   handset, `mre_kernel_main(dtb)` received a wrong/zero address.
2. **Fail path = blind park** — on any FDT/discovery failure `mre_kernel_main`
   parked in `wfe` without touching the APSS watchdog. The XBL leaves the SoC
   watchdog armed; an unsupervised watchdog bites ~40 s after every reset.
3. **Persisted recovery restart reason** — ABL re-boots p77 after each watchdog
   reset, so every cycle re-launched the crashing MRE image: an infinite loop.

The flashed payload was also the wrong tenant for p77: QEAFIVELS OS itself lives
in p77, so the flash removed the running OS from the device.

## 5. Source fixes (MRE-Os)

- `kernel/arch/aarch64/boot/entry.S` — DTB is taken from **x0** (x2 kept only as
  a QEMU-loader fallback).
- `kernel/core/violet.c`:
  - console discovery now accepts `qcom,geni-debug-uart` (the live violet tree's
    debug console) in addition to `qcom,msm-geni-console`;
  - an early GENI console (fallback base 0x880000, FIFO depth from HW_PARAM_0)
    logs entry, DTB address, FDT status, discovery results and the final park
    state before any other driver runs;
  - every failure/success park path now goes through `park_alive()`: the APSS
    watchdog (discovered base, or fallback 0x17C10000) is kicked forever, so a
    failed bring-up is a stable hang, never a reset loop.

## 6. Build / regression

- Host CTest: 4/4 PASS (mre_core_tests, mre_kernel_core_tests, mre_ui_tests,
  mre_violet_reference_tests).
- AArch64 cross-compile: 12/12 sources PASS; `mre_kernel.elf` links clean.
- Repackaged `Image.gz-dtb` (root compatible `xiaomi,violet`, footprint
  0x603b0) and `BOOTIMG.BIN` (114,688 bytes) with the fixed kernel and the
  memory-patched violet DTB.

## 7. Device recovery plan (executed / pending)

1. Enter fastboot (Power + Volume Down). **Done once during triage.**
2. `fastboot flash recovery out/p77-backup-20261114-061514.img` — restores the
   QEAFIVELS boot image and its partition (user-approved; sha256 verified).
3. `fastboot reboot` — stock p76 path boots, then Power + Volume Up boots the
   restored QEAFIVELS from p77; verify the QEAFRESCUE LUN and QEAFBOOT.LOG.
4. **Never overwrite p77 with an MRE payload again.** MRE testing must use
   `fastboot boot build-aarch64/BOOTIMG.BIN` (RAM-only, no partition write) and
   only after that image is proven reset-free.

## 8. On-device verification (2026-11-23, post-fix)

- QEAFIVELS restored to p77 from the verified backup via `fastboot flash recovery` (OKAY, 64 MiB).
- Fixed MRE image booted via **`fastboot boot`** (RAM-only, no partition write).
- Observed 180+ s with no USB re-enumeration and no reset: the pre-fix behaviour
  (watchdog bite ~40 s, ABL re-entering recovery, endless loop) did not recur.
  The APSS watchdog keep-alive (`park_alive`) holds the device stable.
- Bring-up build has no UI yet, so the panel stays dark while the kernel is parked.
- To leave the MRE bring-up: hold Power ~10 s (reboot → stock p76); hold
  Power + Volume Up at boot to enter the restored QEAFIVELS from p77.

## 9. Remaining blockers

- No UART pins hooked to the host: the early console log is only readable with a
  serial probe on the board; without it, boot progress stays inferred from
  reset/no-reset behaviour.
- The exact ABL kernel load address and EL state at handoff remain unverified;
  the bring-up kernel is PC-relative-safe (empty `.data`), but this must be
  confirmed before any MMU-enabling stage.

## 10. Final deployment (2026-11-23)

MRE OS is installed as the device OS: the final image (fixed kernel + stock
DTB from the handset's own boot dump with the memory node patched) was flashed
to **both** p76 (boot) and p77 (recovery), so every boot path converges to MRE.
Post-flash the handset stayed up 190+ s with no USB re-enumeration and no reset
(watchdog keep-alive working). QEAFIVELS remains restorable from
`out/p77-backup-20261114-061514.img` via fastboot; p76's stock image is
recoverable from the QeafdropOs stock-violet dump.

Key evidence corrections from this round:
- The handset's stock boot tree is `qcom,sm6150-idp` + violet dtbo overlay
  (proven by the p76 dump), so the board gate accepts that identity; the
  earlier "generic IDP is never accepted" policy was evidence-contradicted.
- The stock tree ships `qcom,msm-watchdog` @ 0x17C10000 and unplaced
  `shared-dma-pool` nodes that ABL resolves before handoff; the flash-time
  gate tolerates the latter, the kernel still gates on runtime resolution.
