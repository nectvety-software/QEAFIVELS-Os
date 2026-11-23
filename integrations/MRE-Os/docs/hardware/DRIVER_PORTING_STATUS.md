# Driver Porting Status — Violet / MRE AArch64 Kernel

Last updated: 2026-11-23

Status values: UNKNOWN, DISCOVERED, ANALYZED, BUILDABLE, HARDWARE_TESTED, STABLE, BLOCKED.

## Kernel core

| Component | Status | Evidence / limitation |
|---|---|---|
| AArch64 EL2 → EL1 entry | BUILDABLE | Clang AArch64 assembly compilation and ELF link pass |
| Exception vectors | BUILDABLE | 2 KiB-aligned VBAR table; assembly cross-compiled |
| Exception frame / dispatcher | BUILDABLE | Frame save/restore and dispatch compile; no live exception injection |
| FDT parser | BUILDABLE | Parses supplied SM6150 IDP reference DTB and synthetic Violet test DTB |
| FDT reservation map | BUILDABLE | Parses 64-bit reserve-map entries and reserved-memory child nodes |
| 4-level MMU page tables | BUILDABLE | Host tests cover 4 KiB pages, 2 MiB/1 GiB blocks and block splitting |
| MMU enable routine | BUILDABLE | SCTLR/TCR/MAIR/TTBR code compiled; not executed on handset |
| GICv3 | BUILDABLE | Distributor/redistributor and CPU interface code compiled; no live register test |
| Generic Timer | BUILDABLE | CNTFRQ/CNTP driver and GIC PPI registration compiled; no live timer IRQ |
| Qualcomm GENI UART | BUILDABLE | Register/FIFO driver compiled; host FIFO mock test passes |
| Violet runtime board identity | BLOCKED | Supplied DTB root is qcom,sm6150-idp, not xiaomi,violet |
| Violet physical RAM map | BLOCKED | Reference memory node has zero-sized reg; runtime memory map unavailable |
| Secure/dynamic carveouts | BLOCKED | Reference DTB contains 7 dynamic reserved nodes without fixed reg |
| Boot image packaging | BLOCKED | Exact Violet DTB and verified boot handoff layout unavailable |
| Temporary fastboot boot | BLOCKED | No phone connected; ADB and fastboot report no devices |

## Host test evidence

CTest: 3/3 passed.

- Device registry, driver registry and firmware policy tests.
- FDT, reservation map, Violet discovery, MMU block/page-table, timer arithmetic and mocked GENI UART tests.
- Reference-DTB parser test: 652,606-byte DTB parsed; 20 static reserved regions and 7 dynamic reservations found; MMU correctly gated.

## AArch64 build evidence

- 12 AArch64 C/assembly source files compile with clang --target=aarch64-none-elf.
- mre_kernel.elf links successfully as ELF64/AArch64.
- Entry point: link-time offset 0x81000; vector table: 0x81800.
- llvm-readelf -r: no relocations.
- llvm-nm -u: no undefined symbols.
- This ELF is a cross-build artifact, not a verified handset boot image.

## Safety gates

- MMU page tables are built only when the runtime FDT identifies xiaomi,violet, supplies a valid RAM map, and every reserved-memory allocation has a fixed range.
- MMU enable is an explicit API; kernel entry does not enable it automatically.
- GIC, timer and UART hardware initialization are not run against the generic IDP reference.
- No boot, recovery, modem, DTBO, boot or other partition has been written.
