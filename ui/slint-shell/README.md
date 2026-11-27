# QEAFIVELS Slint Shell Prototype

This is an isolated replacement-UI prototype. It does not replace the current QEAF shell, PID 1, recovery, or boot image.

## UI

- Slint 1.19.1 with Rust.
- `ui/main.slint`: Home, Control Center, and Lock Screen.
- `ui/wallpaper.svg`: scalable diagonal-stripe wallpaper.
- Home remains rendered beneath the Control Center. The dark overlay and translucent tiles provide a glass-tinted effect; the software renderer does not apply a true Gaussian background blur.
- Gestures: swipe down from the top-right to open Control Center; swipe up from the bottom to dismiss it; swipe up from the bottom of Lock Screen to return Home.
- Wi-Fi, Bluetooth, brightness, and volume tiles currently update prototype UI state only. They are not yet connected to QEAF system services or kernel drivers.

## Build

Host preview (windowed software renderer):

```sh
cargo build --release --no-default-features --features host-preview
```

LinuxKMS software renderer on a native Linux ARM64 target:

```sh
cargo build --release --no-default-features --features violet-kms
SLINT_BACKEND=linuxkms-software ./target/release/qeaf-shell
```

Cross-build for Violet using an ARM64 GNU/Linux sysroot:

```sh
SYSROOT=/path/to/aarch64-sysroot ./build-violet-arm64.sh
```

The `violet-kms` feature enables Slint LinuxKMS, libinput, and the software renderer. LinuxKMS uses DRM dumb buffers for software scanout; libinput provides pointer/touch/keyboard events. The cross-built executable is dynamically linked against the target's fontconfig/freetype, libinput, libudev, xkbcommon, and their runtime dependencies.

## Validation status

- Slint UI compilation: passed on host and during AArch64 `cargo check`.
- Linux x86_64 build with both host-preview and LinuxKMS features: passed.
- WSL Xvfb runtime smoke test at 393x852: app remained alive for the 8-second test window.
- Full AArch64 GNU/Linux LinuxKMS executable: built successfully at `dist/qeaf-shell-violet-aarch64`.
- Artifact: 13,435,832 bytes; SHA-256 `8dc93504f95f61a71b2a9c9e42f1dcf75c505d1f8d959f125a0b035f5833694c`.
- ELF verification: 64-bit, little-endian, AArch64 PIE; dynamic interpreter `/lib/ld-linux-aarch64.so.1`.
- QEMU AArch64 user-mode smoke test loaded the executable and reached Slint's DRM initialization. It then stopped as expected because the emulation host has no `/dev/dri` or `/dev/fb*`; this is not a full guest-VM display test.
- Not yet run on Violet. Confirm the QEAF root filesystem ABI and install/ship all required shared libraries before launch; a glibc-linked binary must not be assumed compatible with a musl-only root filesystem.
- Not integrated into the production QEAF image and not loaded onto Violet. No partition was written.

## ARM64 virtio-gpu guest validation (2026-11-26)

- Executed the AArch64 LinuxKMS/software-renderer binary in an Ubuntu 24.04.5 ARM64 QEMU guest with `virtio-gpu-pci`.
- Guest kernel initialized `virtio_gpu`, reported one scanout, and created `virtio_gpudrmfb` (`fb0`).
- The Slint app rendered the Home UI to the VNC scanout at 1280x800. The guest copy SHA-256 matched the host artifact.
- Guest runtime dependencies resolved after installing `libinput10` and its ARM64 dependencies. Launching as an unprivileged user displayed a `/dev/tty0` VT-permission warning; launching via sudo displayed the UI.
- **Integration blocker:** QEAF's staged userspace is Android/Bionic (`linker64`, `system/lib64/libc.so`), while this binary requires glibc (`/lib/ld-linux-aarch64.so.1`, `libc.so.6`). It is not directly runnable in the current QEAF rootfs. Do not replace Bionic's libc/linker.
- The 1280x800 VM run proves the generic ARM64 virtio-gpu scanout path, not SM6150/Violet DRM. The UI currently has a fixed 393x852 canvas; full-screen scaling and physical touch gestures still need validation.
- Full report: `docs/SLINT_ARM64_VIRTIO_GPU_TEST_2026-11-26.md`.

## Bionic ABI probe (2026-11-26)

- Added isolated compile probe: `ui/slint-bionic-probe/`.
- `cargo check --target aarch64-linux-android` passed with Slint 1.19.1, software renderer, embedded UI assets, and a custom `Platform` using `MinimalSoftwareWindow`.
- Rust target config confirms `target_os=android`, `target_arch=aarch64`, `target_pointer_width=64` (Bionic target ABI).
- The official Slint Android Activity backend was also evaluated, but it requires Android `NativeActivity` and an installed Android SDK platform. QEAF is a standalone native OS shell, so this is not the selected production backend.
- This is a compile/type-check only, not a linked ELF or runtime test against QEAF's exact Bionic libraries. Custom DRM/KMS scanout, evdev input, clipboard and event loop remain to implement.
- Report: `docs/SLINT_BIONIC_ABI_PROBE_2026-11-26.md`.


## Bionic backend implementation pass (2026-11-26)

- Added isolated `ui/slint-bionic-probe/src/kms.rs`: DRM card/master, connected connector/mode discovery, RGB565 dumb buffer, framebuffer and legacy CRTC setup, mapped scanout copy respecting DRM pitch.
- Added `src/evdev.rs`: nonblocking evdev reader for ABS_X/Y, ABS_MT_POSITION_X/Y, BTN_TOUCH and key events, with caller-supplied axis calibration.
- Added `src/qeaf_clipboard.rs` and wired Slint Platform clipboard methods to a bounded Unix socket protocol (`/run/qeaf/clipboard.sock`, UTF-8, 4096-byte cap, 150ms timeout).
- `cargo check --target aarch64-linux-android` passes with these modules included.
- Important: these are backend primitives, not yet a complete runtime backend. KMS is not yet connected to Slint's renderer/event loop, has no page-flip/vblank buffering and has not been tested on Violet; evdev events are not yet dispatched to Slint and touch axis calibration is pending. Clipboard IPC client is implemented, but QEAF's socket server/permissions are not.
- Android NDK was not found in the searched paths, so no Bionic executable has been linked and no `DT_NEEDED`/symbol audit has been run.
- No RAM-boot image was created; production LVGL remains unchanged; no device partition was written.
- Full implementation status and remaining gates: `docs/SLINT_BIONIC_BACKEND_IMPLEMENTATION_2026-11-26.md`.

## Bionic runtime backend completion pass (2026-11-26)

- Slint now owns the DRM/KMS + evdev event loop: timer updates, pointer/key dispatch, software RGB565 rendering, dumb-buffer scanout, connector→encoder→compatible CRTC selection, and best-effort prior CRTC restore.
- Touch calibration queries EVIOCGABS automatically (MT axes 53/54, fallback ABS_X/Y); explicit QEAF_TOUCH_* overrides remain available.
- Added standalone `qeaf-clipboardd` with bounded UTF-8 IPC and 2/2 host unit tests passing.
- Installed Android NDK r30 LTS under WSL `/opt/android-ndk-r30`; archive SHA-1 verified. Linked both ARM64 executables using the staged `build/violet/bionic-sysroot` libraries.
- Slint ELF: PIE AArch64, interpreter `/system/bin/linker64`, DT_NEEDED `libdl.so`, `libm.so`, `libc.so`; 79/79 unique dynamic import names resolve in staged Bionic libs.
- Clipboard daemon ELF: PIE AArch64, same Bionic interpreter; DT_NEEDED `libdl.so`, `libc.so`.
- Remaining: no physical Violet DRM/touch run, no page-flip/vblank or DRM master-loss handling, no PID 1 daemon startup/group policy, and no runtime execution under the device's linker. Keep LVGL production image unchanged; do not package RAM-boot until these gates pass.
- Rebuild: `tools/build-slint-bionic.sh`; detailed report: `docs/SLINT_BIONIC_BACKEND_IMPLEMENTATION_2026-11-26.md`.

### Page-flip and init hook update (2026-11-26)
- DRM scanout now uses two RGB565 dumb buffers and legacy page-flip with completion event (vblank-synchronized), 1s timeout and CRTC event validation.
- Clipboard daemon accepts optional QEAF_CLIPBOARD_GID for socket group ownership; `system/init/start-clipboardd.sh` is the PID-1 startup hook, waiting for socket readiness.
- PID-1 caveat: current `system/init/init.c` is a state helper, not the complete PID-1 service supervisor; actual init entrypoint wiring is still required.
- Host clipboard tests 2/2 and ARM64 Bionic build pass. No Violet device was attached to ADB/Fastboot, so hardware DRM/touch test is pending. No RAM-boot image or partition writes.
