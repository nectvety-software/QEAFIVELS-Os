# Slint Bionic ABI probe — 2026-11-26

## Scope

An isolated probe was added under `ui/slint-bionic-probe/`. It reuses the production QEAF Slint UI source at `ui/slint-shell/ui/main.slint`, enables Slint 1.19.1 software rendering, and registers a custom `slint::platform::Platform` using `MinimalSoftwareWindow`.

This is deliberately separate from the production image and the existing shell.

## Verified

Command (run from WSL Ubuntu 24.04):

```sh
source "$HOME/.cargo/env"
cd /mnt/d/QEAFIVELS-Os/ui/slint-bionic-probe
CARGO_TARGET_DIR=/tmp/qeaf-slint-bionic-core-target \
CARGO_BUILD_JOBS=2 CARGO_PROFILE_DEV_DEBUG=0 \
cargo check --target aarch64-linux-android
```

Result: `Finished dev profile` with exit code 0.

Rust target configuration reports:

- `target_arch="aarch64"`
- `target_os="android"`
- `target_pointer_width="64"`
- little-endian, ARM64

The probe therefore type-checks the Slint UI and custom Platform entrypoint for the Android/Bionic AArch64 target. The software-renderer embedded-resource configuration is used for the SVG wallpaper and UI assets.

## Android Activity backend finding

A separate attempt to enable Slint's `backend-android-activity-06` stopped in its build script with `No Android platforms found` because no Android SDK platform / `android.jar` is installed in this WSL environment.

More importantly, that backend expects Android `NativeActivity`. QEAF is a standalone native OS shell, not an Android application process hosted by ActivityManager. Do not make the Activity backend the production QEAF shell merely because it targets Bionic.

## Important limitations

- `cargo check` is a compile/type-check only; it does not link a final Android shared object or executable.
- No Android NDK is installed in the current WSL environment, so there is no final Bionic-linked ELF yet.
- The custom Platform currently uses `MinimalSoftwareWindow` as a compile probe. It does not yet implement QEAF's DRM dumb-buffer/atomic-KMS scanout, evdev input dispatch, clipboard bridge, or event loop.
- No binary was copied into the device image and no phone partition was changed.
- The tested ABI target is Bionic-compatible AArch64 at the Rust target level. Runtime symbol/API compatibility with the exact QEAF Bionic build still needs a linked artifact and on-device smoke test.

## Storage note

The first compile wrote about 1.2 GiB of generated Cargo output under the Windows-mounted `D:` path and hit an I/O error because that volume was full. That generated `ui/slint-bionic-probe/target` directory was removed. The successful check writes to WSL's Linux filesystem under `/tmp`, not to the Windows-mounted project directory. At the end of the test, D: still had only about 1.2 GiB free; avoid placing future Cargo target output on D: until space is recovered.

## Next engineering steps

1. Add the Android NDK (in WSL's Linux filesystem) and link the custom-platform probe for `aarch64-linux-android`.
2. Replace the placeholder window with a QEAF backend: DRM dumb buffer or KMS framebuffer target, pixel-format/stride negotiation, and a vsync-aware redraw loop.
3. Bridge evdev multitouch/key events to Slint `WindowEvent`; add clipboard callbacks to QEAF's clipboard service.
4. Run the linked binary against the exact QEAF Bionic libraries in a staging rootfs; inspect `DT_NEEDED`, symbol versions and unresolved symbols.
5. Only then package a separate RAM-boot image for Violet. Keep production boot and partition state unchanged until the RAM test passes.
