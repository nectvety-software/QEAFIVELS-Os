# QEAFRESCUE v1 — USB/Fastboot, image verification and RAM boot

## Scope

v1 host tooling provides read-only USB discovery, Android boot-image validation,
SHA-256 verification, strict Fastboot product validation, and an explicit
temporary boot command. It intentionally does not implement partition writes.

## Commands (Windows PowerShell)

From the repository root:

```powershell
python scripts/qeafrescue.py detect
python scripts/qeafrescue.py verify --image dist/boot-vqeaf-violet.img
python scripts/qeafrescue.py verify --image dist/boot-vqeaf-violet.img --expect-sha256 72d3b71d5e49154f8a0320c4a0839d86c9cf4d2de073dc78adb4047771431884
python scripts/qeafrescue.py ram-boot --image dist/boot-vqeaf-violet.img
python scripts/qeafrescue.py ram-boot --image dist/boot-vqeaf-violet.img --execute
```

The last command is the only command that sends a boot operation to the phone.
It uses `fastboot boot`, which requests a temporary in-memory boot and does not
issue `flash`, `erase`, `format`, or `update`. There is no automatic
fallback to a persistent write.

## Device gate

Before RAM boot, the tool requires exactly one Fastboot device (or an explicitly
selected serial), queries `fastboot getvar product`, and requires exact
`violet`. It requires an explicit unlocked response and rejects locked or
unknown unlock states. The phone is not
automatically rebooted into Fastboot; the operator enters Fastboot manually.

## Image gate

The verifier checks the `ANDROID!` magic, supported boot header version
(0–2), power-of-two page size, non-empty kernel and ramdisk, and that declared
payloads fit within the file. It reports the full SHA-256 and boot parameters.
For production use, pass the expected SHA-256 from a trusted build manifest.

## Current image record

At implementation time the image is:

- Path: `dist/boot-vqeaf-violet.img`
- Size: 32,403,456 bytes
- SHA-256: `72D3B71D5E49154F8A0320C4A0839D86C9CF4D2DE073DC78ADB4047771431884`
- Magic: `ANDROID!`
- Header version: 1
- Page size: 4096 bytes

This verifies its container structure only. It does not prove the kernel can
boot on Violet or that Wi-Fi works.

## Not yet implemented

QEAFRESCUE LUN/MTP command transport integration, backup/restore, partition
installer, and persistent recovery logs are later phases. No partition is
written by v1. Hardware RAM boot remains pending until Fastboot detects the
phone and confirms `product=violet`.
