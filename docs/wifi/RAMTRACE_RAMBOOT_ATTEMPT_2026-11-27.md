# QEAF Violet RAM-only trace boot attempt — 2026-11-27

## Build and image
- /init rebuilt successfully with VQEAF_RAM_ONLY_DIAGNOSTICS.
- Boot journal writes are refused in this diagnostic build; App Store path is /tmp/qeaf-appstore.bin.
- Live Wi-Fi trace is /tmp/qeaf-wifi-trace.log.
- At attempt time, 18/18 Python Wi-Fi/RAM-trace contract tests passed and 14/14 CTest checks passed. These are historical counts for this image, not the current release-suite totals.
- Image: dist/boot-vqeaf-violet-source-ramtrace.img
- Size: 49,029,120 bytes
- SHA-256: 820947c99f442c0de6655c902dc92bf0b71bcaea18163b3be154a6495f610d20
- Source kernel contains the tagged skip_initramfs override; appended DTB and boot-image structural/rescue audits passed.

## Device attempt
- Fastboot serial: ca1af25d
- fastboot getvar product: violet
- One command only: fastboot boot dist/boot-vqeaf-violet-source-ramtrace.img
- Fastboot transfer and boot command both returned OKAY, exit code 0.
- After 47.12 seconds, ADB and Fastboot were absent.
- USB only showed generic mass storage VID_349C:PID_0418, serial 20240418000000; QEAF expected descriptor VID_1d6b:PID_0104, serial vqeaf-os-violet was not observed.

## Runtime conclusion
- Fastboot accepted the boot command only; kernel handoff and runtime were not independently confirmed.
- Initramfs unpack, QEAF /init as PID 1, and QEAF USB gadget are not confirmed.
- Do not infer Wi-Fi readiness from this attempt.
- WLAN-PD was not enabled; WLFW was not started; QRTR 0x45, wlan0, scan, association and DHCP were not tested.

## Safety
- RAM boot only.
- Partition writes: 0.
- Erase/format: 0.
- Diagnostic build disables boot-journal writes to p77 and routes appstore writes to tmpfs.
- No firmware, BDF, MAC or calibration data was modified.
- Do not retry WLAN-PD until boot failure is diagnosed from UART/pstore and PID 1 is confirmed.
