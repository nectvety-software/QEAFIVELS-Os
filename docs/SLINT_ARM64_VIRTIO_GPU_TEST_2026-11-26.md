# Slint ARM64 LinuxKMS / virtio-gpu validation — 2026-11-26

## Result

The Slint shell was executed as an AArch64 Linux application inside an ARM64 QEMU guest and rendered the QEAF home screen to a QEMU `virtio-gpu-pci` scanout using Slint's software renderer. A screenshot was captured from the QEMU VNC display.

This validates the LinuxKMS/software-renderer path on a generic ARM64 Linux guest. It does **not** validate the Qualcomm SM6150 DRM driver, the Violet panel, or the physical phone.

## Guest configuration

- Host: Windows 11 with WSL2 Ubuntu 24.04.
- QEMU system: `qemu-system-aarch64` 8.2.2.
- Guest: Ubuntu Server 24.04.5 LTS, AArch64, Linux `6.8.0-147-generic`.
- Machine: `virt`, TCG, Cortex-A57, 4 vCPU, 4 GiB RAM.
- Display: `virtio-gpu-pci`, one scanout, VNC 1280x800.
- Input devices: `virtio-keyboard-pci`, `virtio-tablet-pci`.
- Firmware: AAVMF AArch64 UEFI.
- Guest disk: qcow2 overlay over the downloaded Ubuntu ARM64 cloud image; test-only, separate from QEAF images.

Guest kernel log confirmed:

```text
virtio-gpu-pci detected
number of scanouts: 1
Initialized virtio_gpu 0.1.0
fb0: virtio_gpudrmfb frame buffer device
```

## Slint artifact

- File: `ui/slint-shell/dist/qeaf-shell-violet-aarch64`
- Size: 13,435,832 bytes.
- SHA-256: `8dc93504f95f61a71b2a9c9e42f1dcf75c505d1f8d959f125a0b035f5833694c`.
- ELF: 64-bit little-endian AArch64, PIE.
- ELF interpreter: `/lib/ld-linux-aarch64.so.1`.
- `DT_NEEDED` includes `libc.so.6`, `libm.so.6`, `libgcc_s.so.1`, `libxkbcommon.so.0`, `libudev.so.1`, `libinput.so.10`, and `libfontconfig.so.1`.

The guest initially reported `libinput.so.10` missing. Installing the Ubuntu ARM64 `libinput10` package and its dependencies resolved all `ldd` dependencies. The guest copy's SHA-256 matched the host artifact.

The app log reported `Using Software renderer` and `Rendering at 1280x800`. Running it as an unprivileged user displayed the UI but reported that it could not open `/dev/tty0` for virtual-console preparation. Running it through `sudo` displayed the home UI without that permission issue. Production launch must define a deliberate VT/DRM permission and seat policy rather than relying on an interactive root shell.

## QEAF runtime ABI comparison

The current Violet initramfs staging tree contains Android/Bionic runtime components:

- `build/violet/stage/system/bin/linker64`
- `build/violet/stage/system/lib64/libc.so`
- `build/violet/stage/system/lib64/libdl.so`
- `build/violet/stage/system/lib64/libm.so`
- QEAF's `ld-qeafiv2.so` compatibility/runtime shim

The staged `/init` is an AArch64 ELF without a `PT_INTERP` segment. The Slint executable, by contrast, requests the glibc loader `/lib/ld-linux-aarch64.so.1` and glibc SONAMEs. The staged QEAF runtime is not a musl rootfs and does not provide that glibc loader.

**Conclusion:** the tested Slint executable must not be copied into the current QEAF image and expected to run directly. Do not replace Bionic's `libc.so` or `linker64` with glibc files.

## Follow-up before production integration

1. Choose an isolated application ABI strategy: either a separately packaged glibc sidecar runtime (without replacing Bionic), or a native Bionic-compatible build after verifying that Slint's LinuxKMS/libinput backend supports the chosen Android target and sysroot.
2. Keep the LinuxKMS display backend and input backend separable. The current `violet-kms` feature links libinput; QEAF may instead need to bridge its existing evdev/input service.
3. Make the 393x852 design canvas responsive or establish the actual LinuxKMS scale-factor policy. The successful VM run used a 1280x800 scanout with a fixed 393x852 UI canvas, so it proved rendering but not full-screen layout adaptation.
4. Validate swipe/Control Center input using a real libinput touch event stream; the QEMU pointer interaction was not a complete touch-gesture test.
5. Only after those checks, package a separate test image and run it with `fastboot boot` on Violet. No physical-device image was changed during this VM test.

## Safety / deployment status

- No QEAF boot image was modified by this test.
- No phone partition was flashed, erased, or formatted.
- No physical-device boot test was performed.
- QEMU guest files are under `build/virtual-arm64/` and are disposable test artifacts.
