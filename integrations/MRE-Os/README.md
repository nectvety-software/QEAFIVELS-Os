# MRE OS — Violet

MRE OS is an independent AArch64 operating-system project targeting Xiaomi Redmi Note 7 Pro (codename violet, Qualcomm SM6150 / Snapdragon 675).

## Architecture

Bootloader → MRE AArch64 Kernel → MRE HAL → MRE Drivers → System Services → Framework → Runtime → UI.

This project does not use Android Framework, ART, Dalvik, Android userspace, or a Linux kernel as its runtime foundation. Linux SM6150 sources and Android device trees are hardware-reference material only.

## Current implementation status

- Device Manager, Driver Manager and Firmware Manager: host-buildable.
- AArch64 EL2-to-EL1 entry, exception vectors and C exception dispatcher: implemented and cross-compiled.
- FDT parser: supports v17 node/property traversal, reg tuples and 64-bit reservation maps.
- MMU: 4-level tables, 4 KiB pages, 2 MiB/1 GiB blocks, block splitting, W^X, reserved-range exclusion and explicit enable routine.
- GICv2/v3, ARM Generic Timer and Qualcomm GENI UART: initial driver implementations compile.
- Violet discovery and boot report: implemented; exact board DTB is still required.
- Native UI home-shell foundation: RGB565 framebuffer renderer, Vietnamese UTF-8 font callback, status panel, four app tiles and hit testing.
- Host CTest: 4/4 passed, including the new UI tests.
- Native UI module cross-compiled to an ELF64/AArch64 object.
- AArch64 kernel cross-compile: 12 source files passed; mre_kernel.elf links as ELF64/AArch64 with no relocations or undefined symbols.
- Hardware boot: BLOCKED pending an exact live Violet DTB and a connected test handset.

## Host build

Run the Windows host build and tests:

    .\tools\build_host.ps1

Current tests cover driver/device registries, firmware policy, FDT and reservation parsing, Violet discovery, MMU page/block mapping, timer arithmetic and mocked GENI UART FIFO writes.

## AArch64 build

The MSYS2 LLVM/Clang AArch64 toolchain is used for the freestanding build:

    .\tools\verify_aarch64_compile.ps1
    .\tools\build_aarch64.ps1 -LinkAddress 0x81000

The link address is an ARM64 Image-relative link-time offset, not a verified physical RAM base. The ELF is a cross-build artifact, not a bootable Android boot.img.

## Violet DTB gate

The available prebuilt Image.gz-dtb currently identifies qcom,sm6150-idp, not xiaomi,violet. Its memory node is zero-sized and it has seven dynamic reserved-memory nodes. The MRE board-identity and MMU gates reject this generic IDP tree.

The kernel packager requires a confirmed Violet DTB:

    python .\tools\package_violet_kernel.py --elf .\build-aarch64\mre_kernel.elf --dtb .\path\to\violet-live.dtb --objcopy C:\msys64\mingw64\bin\llvm-objcopy.exe --nm C:\msys64\mingw64\bin\llvm-nm.exe --output .\build-aarch64\Image.gz-dtb

It refuses generic SM6150/IDP DTBs, missing RAM ranges and unresolved dynamic reservations. No Image.gz-dtb or boot.img has been packaged from the current reference.

## Temporary boot test

After an exact Violet boot.img is built and verified, the prepared script uses fastboot boot only:

    .\tools\fastboot_boot_test.ps1 -BootImage .\build-aarch64\boot.img

It checks that exactly one fastboot device is connected, product is violet, and the bootloader reports unlocked. It contains no flash, erase or partition-write command. No device is currently connected, so no boot test has been run.

## Safety

Builds and host tests do not write to a phone. Never flash boot, modem, persist, EFS or firmware partitions as part of the default workflow. Proprietary firmware is not bundled; verify origin, checksum, device compatibility and redistribution rights before use.

See:
- docs/hardware/DRIVER_PORTING_STATUS.md
- docs/hardware/KERNEL_BOOT_STATUS.md
- docs/hardware/DRIVER_SOURCE_MAPPING.md
- docs/hardware/BOOT_CHAIN_ANALYSIS.md
