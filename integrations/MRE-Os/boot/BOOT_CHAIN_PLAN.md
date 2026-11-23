# MRE Boot Chain Plan — Violet

## Independence target

After handoff to MRE, execution must remain entirely within the MRE kernel, HAL, native drivers, services, framework, runtime and UI. Android Framework, ART, Android userspace and a Linux kernel are not runtime dependencies.

## Hardware boot-chain boundary

The Qualcomm Primary Boot ROM (PBL) is mask-ROM code inside the SoC and is not a replaceable phone partition. Later stages (for example XBL/ABL and Android boot-related partitions) are device-firmware components whose names, formats, signing policy and update rules must be read from the exact handset. The existence of an unlocked bootloader does not by itself prove that arbitrary replacement of ABL/XBL is accepted.

The safe engineering target is a native MRE boot manager / second-stage loader that validates and loads the MRE kernel and its board data. Whether it can become the default boot stage by replacing a later firmware partition is a separate, device-specific operation and remains unapproved until all gates below are met.

## Required gates before any partition write

1. Identify exact Violet hardware revision, bootloader version and lock state from the handset.
2. Read the live GPT and record exact partition names, sizes, GUIDs and offsets. Do not infer them from another Redmi model or a generic SM6150 target.
3. Obtain exact stock firmware for this device and build. Verify hashes and retain recoverable copies of every partition that could be affected.
4. Determine the accepted boot image and firmware container formats, AVB/signature chain, rollback indexes and anti-rollback behavior.
5. Confirm the recovery path (fastboot restore or exact Qualcomm EDL/firehose package) before modifying anything.
6. Validate the MRE loader and kernel in a non-persistent boot path first, with UART/serial or another observable diagnostic path.
7. Require an explicit, separate authorization for each destructive write after showing the exact partition, image, size and hash.

## Current state

- No handset is connected to ADB or fastboot.
- No live Violet DTB or live GPT has been acquired.
- The only available reference FDT identifies qcom,sm6150-idp, not Violet.
- No MRE boot image has been packaged or booted on hardware.
- No bootloader or phone partition has been written.
- The current project contains no partition writer and no command that flashes or erases the handset.

## Next non-destructive step

Connect the unlocked Violet phone with USB data cable, enable USB debugging, accept the host authorization prompt and run `adb devices -l`. Once ADB reports the handset, acquire `/sys/firmware/fdt` read-only where permitted, then collect exact build fingerprint, bootloader properties and partition metadata. If the device cannot expose the live FDT, use an exact stock Violet boot image and matching DTBO as offline evidence.

Do not substitute an IDP tree. Do not attempt a bootloader write until the device-specific recovery path is proven.
