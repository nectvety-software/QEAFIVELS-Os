# QEAF Violet initrdfix RAM boot attempt — 2026-11-27

## Result

One RAM-only Fastboot boot command was issued against the initrdfix candidate. The bootloader accepted and started the command, but QEAF runtime did not become verifiable through USB.

- Target: Xiaomi Redmi Note 7 Pro (violet)
- Image: D:\QEAFIVELS-Os\dist\boot-vqeaf-violet-source-initrdfix.img
- Image size: 49,029,120 bytes
- SHA-256: d9839a7d09f450b684a66cb07ca1f2c3c823d86a8c8eace767cbe1f05e7b03c7
- Fastboot product: violet
- Fastboot output: Sending 'boot.img' (47880 KB) OKAY; Booting OKAY; exit code 0
- Attempts: one invocation of fastboot boot; no second attempt was made.

## USB/runtime observation

After the command, the phone did not enumerate as ADB or Fastboot for at least 51 seconds. A subsequent host check also found no ADB or Fastboot target.

The QEAF USB gadget source configures these descriptors before binding the rescue LUN:
- VID:PID 1d6b:0104
- Serial: vqeaf-os-violet

Windows did not observe that descriptor. It did observe a generic mass-storage device 349c:0418 with serial 20240418000000, parented to generic USB hub 214b:7260. Windows reports its manufacturer as “Compatible USB storage device”. This does not match the QEAF USB gadget contract and is not treated as QEAF evidence.

Therefore:
- Kernel override log: not observed
- Initramfs unpack: not confirmed
- QEAF /init as PID 1: not confirmed
- QEAF rescue gadget: not confirmed
- Root cause after handoff: unknown until retained console/pstore or UART is collected.

Fastboot OKAY proves only that Fastboot accepted and transferred the image; it does not prove the kernel or /init completed boot.

## Runtime provider gates

No PM6150 rail-vote measurement, ICNSS XO clock state, WLAN SMMU IOVA mapping, CE DMA/IRQ or channel-runtime test was performed. No WLAN-PD process was started and no WLFW handshake was attempted.

## Safety

- fastboot boot only; no fastboot flash.
- Partition writes: 0.
- Erase/format: 0.
- p74/p76/p77 unchanged by this attempt.
- WLAN-PD remains disabled.
- WLFW remains untested.
- Calibration/MAC data unchanged.

## Next recovery / diagnostic gate

The phone is not currently visible to the host over ADB or Fastboot. Restore it to Fastboot using the phone hardware keys (Power + Volume Down) and reconnect/confirm fastboot getvar product returns violet.

To preserve the best chance of retaining the last console:
1. Prefer a warm recovery/reboot path; avoid disconnecting the battery or a long power-off interval.
2. Once Fastboot is visible, boot the normal installed system without flashing and read /sys/fs/pstore/console-ramoops-* using authorized ADB root.
3. Search for qeaf_ramboot_probe=violet-source-4.14.215, the QEAF override log string, “Trying to unpack rootfs image as initramfs”, init execution errors and any panic/oops.
4. If no ramoops console survives, obtain a 1.8 V GENI UART capture (115200 8N1) from the board debug pads before changing the candidate again.

Do not retry the candidate or unlock WLAN-PD/WLFW until the failed handoff is explained and QEAF PID 1 is independently confirmed.
