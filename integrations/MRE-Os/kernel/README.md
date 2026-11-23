# MRE Kernel — implementation gate

This directory is reserved for the independent AArch64 kernel.

Before writing the first bootable image, the project must confirm the Violet bootloader handoff ABI, boot header compatibility, DTB/DTBO order, physical memory map, secure carveouts, GIC state, timer frequency and serial console. Current Android BoardConfig values are reference clues, not an approved MRE boot contract.

The current build target is a host-side library for testing kernel-independent driver-management logic. It is not a kernel image. Do not add Linux kernel code as a runtime dependency.
