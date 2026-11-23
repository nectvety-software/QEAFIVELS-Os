# MRE-Os source import into QEAFIVELS-Os

Imported from `D:\os-system\MRE-Os` on 2026-11-23.

## Import boundary

This directory is a self-contained source import. It preserves the original MRE-Os project layout and its own CMake build, host tests, AArch64 entry, FDT parser, MMU/GIC/timer/GENI UART foundations, Violet discovery and native RGB565 UI module.

Generated build directories and temporary allocator data were excluded from the import. The source project at `D:\os-system\MRE-Os` remains unchanged.

## Why it is isolated

QEAFIVELS-Os already has a separate freestanding AArch64 kernel in `kernel/arch/aarch64` with its own QEMU-virt boot path, scheduler, IPC, syscall/security model, compositor and UI. MRE-Os is an earlier independent kernel foundation with different interfaces and boot assumptions.

Do not overwrite QEAFIVELS kernel sources or add MRE's `mre_kernel.elf` to the production Violet package. The correct integration path is selective porting after API, linker, exception-level, memory-map and driver ownership review.

## Candidate components for selective reuse

1. FDT v17 parser and Violet discovery: compare against the existing QEAF DTB parser and hardware evidence.
2. MMU page/block mapping and W^X checks: compare descriptor format, allocator ownership and current AArch64 page-table implementation.
3. GIC and Generic Timer: compare interrupt routing, per-CPU init and existing QEAF IRQ/timer code.
4. Qualcomm GENI UART: only after Violet clock, pinmux and console routing are confirmed.
5. MRE UI RGB565 renderer: adapt to QEAF's framebuffer/compositor and font APIs; do not introduce a second display owner.
6. Host tests and build scripts: reuse as independent regression fixtures where compatible.

## Validation

The imported source must build and pass its own host tests from this directory. That validates the import and MRE module tests only; it does not establish Violet hardware support.

## Hardware and deployment boundary

This import does not flash or write a phone. It does not change XBL/ABL, boot, recovery, modem, persist, EFS, or any other partition. Device bring-up remains gated on exact Violet DTB, handoff contract, RAM/reserved-memory map, bootloader/AVB state and a recoverable test route.

## Selective integration status — 2026-11-23

The MRE FDT/Violet discovery, MMU page-table builder, GIC and Generic Timer sources are now compiled as part of the QEAF AArch64 kernel build through the existing `kernel/platform/aarch64/` bridge. The QEAF-facing API remains separate from MRE internals.

The integration gate was tightened:

- `LIVE_HANDSET` can mark a board identity as supported evidence, but cannot authorize PMM/MMU/GIC activation.
- `BOOT_HANDOFF` is required by `vqeaf_violet_native_bootstrap`; the report distinguishes board identity from verified boot handoff.
- PMM descriptor generation and the GIC/timer bridge require `boot_handoff_verified`.
- The PMM still rejects any RAM region extending beyond its current 4 GiB managed limit instead of truncating it.
- The native bootstrap does not run from the existing QEMU `_start` path. A Violet-specific linker layout and boot ABI must be established first.

A fresh MinGW host build completed and all 17 CTest cases passed. The AArch64 QEAF kernel also compiled and linked to `build/kernel-aarch64/vqeaf-kernel-aarch64.elf` (582,968 bytes). The imported MRE project regression suite also passed 3/3. These are compile/host-test results only; they do not verify MMIO, IRQ delivery, timer operation, boot handoff, or physical handset compatibility.

No firmware was flashed and no phone partition or boot-chain component was written.
