# Violet initramfs skip-flag diagnosis

## Observed on 2026-11-26

Diagnostic image:
- `dist/boot-vqeaf-violet-diagnostic-20261126.img`
- SHA-256: `1724047675e3e193ed95efba5a7997d516a79d5e6ec4bfa513a8f65399d33a65`

The RAM boot was accepted by Fastboot. The kernel received:
- `init=/init rdinit=/init`
- `androidboot.qeaf_ramboot_probe=violet-v4`
- `skip_initramfs`
- `root=PARTUUID=...`

The kernel log recorded `Freeing initrd memory: 33360K`. PID 1 was
`/system/bin/initsecond_stage`, not QEAF. Therefore the external ramdisk
was supplied but skipped before `/init` could execute.

The official Linux 4.14 Violet source path `init/initramfs.c` implements:
`if (do_skip_initramfs) { ... return default_rootfs(); }`.
The patch in this directory changes that guard to skip only when no external
initrd was supplied.

## Hardware evidence

- Panel bind: `dsi_nt36672a_shenchao_vid_display`
- DRM connector: `card0-DSI-1`, connected, mode `1080x2340x60`
- DRM/KMS initialized successfully.
- DWC3 UDC: `a600000.dwc3`, state `configured`
- USB host exposed Android ADB, not QEAF mass storage.

## Safety

- This patch is a candidate only.
- Exact 4.14.215 source commit is not yet proven.
- It has not been compiled or used in a boot image.
- No partition write, erase, format, or DTBO write was performed.
- Do not build a TWRP install ZIP or write `boot` until the patched kernel
  passes a RAM boot with QEAF PID 1, QEAF USB rescue, and display validation.
