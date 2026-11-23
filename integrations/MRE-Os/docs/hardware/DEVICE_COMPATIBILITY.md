# Device Compatibility — Violet

Target: Xiaomi Redmi Note 7 Pro, codename violet, Qualcomm SM6150 / Snapdragon 675.

## Known target-family data

- CPU family: Kryo 460, 8 cores, AArch64.
- GPU family: Adreno 612.
- Memory: LPDDR4X, target variants listed as 4 GB or 6 GB.
- Display target: 1080 × 2340, nominal 6.3-inch panel.
- Storage target: eMMC 5.1.
- Battery target: approximately 4000 mAh.

These are specification-level values, not measurements from the connected handset. Panel ID, touch controller, PMIC, memory map, DRAM size, storage geometry and board revision remain runtime-discovery items.

## Compatibility gates

- Confirm SoC ID and board revision.
- Parse DTB and validate memory reservations.
- Identify display and touch compatible strings.
- Verify bootloader handoff and secure boot policy.
- Confirm firmware versions and redistribution rights.
- Require boot test and hardware test evidence before marking a subsystem supported.
