# QEAF Slint Bionic backend implementation log — 2026-11-26

## Scope and safety

Implementation is isolated under `ui/slint-bionic-probe/`. Production QEAF LVGL, PID 1, boot image, and device partitions were not modified. No image was packaged or transferred to Violet. No flash/erase/format or partition writes occurred.

## Slint + DRM/KMS + evdev

- `src/main.rs` now owns a Slint `Platform` implementation and `run_event_loop()`.
- The loop updates Slint timers/animations, polls evdev, dispatches `WindowEvent` pointer/key events, calls `MinimalSoftwareWindow::draw_if_needed()`, renders RGB565 with `SoftwareRenderer::render()`, and copies the frame into the DRM dumb buffer for scanout.
- `src/kms.rs` obtains DRM master, selects a connected connector, resolves encoder→compatible CRTC, creates an RGB565 dumb buffer/framebuffer, and saves the prior CRTC framebuffer/mode/position for best-effort restore on drop.
- `src/evdev.rs` reads Linux input events nonblocking; supports ABS_X/Y, ABS_MT_POSITION_X/Y, ABS_MT_TRACKING_ID and BTN_TOUCH. It queries EVIOCGABS for multitouch axes 53/54, falling back to ABS_X/Y 0/1; explicit QEAF_TOUCH_* environment calibration overrides auto-detection.
- Default device paths are `/dev/dri/card0` and `/dev/input/event0`; set `QEAF_DRM_DEVICE` and `QEAF_TOUCH_DEVICE` when the actual Violet node differs.

### Known runtime limitations

This is a working first-pass backend implementation, not yet validated on SM6150 hardware. KMS currently uses one RGB565 dumb buffer and legacy CRTC set/scanout; it has no page-flip/vblank double buffering, DRM master-loss recovery, VT handling, or hotplug recovery. RGB565 scanout support and actual touch event-node/axis behaviour must be confirmed on Violet. No claim is made that the Slint UI has been displayed on the phone.

## QEAF clipboard service

- `src/bin/qeaf-clipboardd.rs` provides a separate Unix-domain socket daemon at `/run/qeaf/clipboard.sock` (override with `QEAF_CLIPBOARD_SOCKET`).
- Socket mode is 0660. Clipboard data is in-memory only, UTF-8 validated, NUL rejected, maximum payload 4096 bytes.
- Protocol: `GET\n` returns `EMPTY\n` or `DATA <byte-count>\n` plus payload. `SET <byte-count>\n` plus payload returns `OK\n`.
- The Slint platform clipboard methods use the matching client in `src/qeaf_clipboard.rs`.
- Host unit tests: 2/2 PASS (empty GET, UTF-8 SET/GET round-trip, oversize request rejection).

The existing `system/core/clipboard.c` remains process-local and is not silently treated as shared memory. The daemon is built as a standalone executable, but PID 1/service startup wiring and the intended application group/GID policy are not yet integrated into the OS image.

## NDK and Bionic link

- Installed Android NDK r30 LTS, version `30.0.16248370`, at `/opt/android-ndk-r30` in WSL.
- Official Linux archive SHA-1 verified: `5107f898313790e449e87eee2183d9a20602dee9`.
- Link target: `aarch64-linux-android`, NDK clang API 24. Cargo output is kept in WSL `/tmp`, not on the nearly-full Windows D: volume.
- Link wrapper: `tools/aarch64-android24-clang-qeaf`. It prioritizes staged project libraries from `build/violet/bionic-sysroot` while retaining NDK CRT/unwind support.
- Staged `system/lib64/libc.so` and `apex/com.android.runtime/lib64/bionic/libc.so` SHA-256 are both `7a27461a75d678f19858a4513228db953fdd92909397899d713707c43b427eee`.
- Link trace confirmed that `libc.so`, `libm.so`, and `libdl.so` were taken from `build/violet/bionic-sysroot/system/lib64`; NDK CRT and unwind objects came from NDK r30.
- The Slint executable is AArch64 PIE with interpreter `/system/bin/linker64`; its DT_NEEDED entries are `libdl.so`, `libm.so`, and `libc.so`.
- Dynamic undefined-symbol audit: 79 unique imports, all 79 names found in the staged Bionic `libc.so`, `libm.so`, or `libdl.so`.
- The clipboard daemon is also linked AArch64 PIE with interpreter `/system/bin/linker64`; DT_NEEDED: `libdl.so`, `libc.so`.

### Build validation

Commands and results:
- `cargo check --target aarch64-linux-android --all-targets`: PASS.
- `cargo build --target aarch64-linux-android --bins`: PASS.
- `cargo test --bin qeaf-clipboardd` on WSL host: PASS, 2/2 tests.
- `readelf` audit: AArch64 ELF64 PIE, expected Bionic interpreter, expected dynamic dependencies.
- Staged Bionic symbol-name resolution: PASS, 79/79.

The ELF has been linked against the staged QEAF Bionic sysroot, but it has not yet been executed by the target device's actual `/system/bin/linker64`. The staged sysroot's identity against the currently running Violet image must still be confirmed before calling this a device-runtime ABI pass.

## Artifact locations

- Slint UI binary: `/tmp/qeaf-slint-android-target/aarch64-linux-android/debug/qeaf-slint-bionic-probe`
- Clipboard daemon: `/tmp/qeaf-slint-android-target/aarch64-linux-android/debug/qeaf-clipboardd`
- Final SHA-256: Slint `b5cc8351fca0208466122c202afe484ccb5f1235e3f40eb8974233ef779d3f67`; clipboard daemon `94522bd47cc97fbdcb31d4fae967f9fb8de4dcc013c87d7f43572f632a0e98d3`.
- Reproducible build script: `tools/build-slint-bionic.sh`.
- ELF audit script: `tools/audit-slint-bionic-elf.sh`
- Undefined-symbol audit: `tools/audit-slint-symbol-resolution.sh`
- Clipboard IPC tests: `src/bin/qeaf-clipboardd.rs`

## Gate before RAM boot

Do not replace LVGL in the production image. Before creating a RAM-boot image: verify the staged sysroot against the exact device image/runtime, install daemon startup and socket group policy, exercise DRM RGB565 scanout and calibrated evdev on Violet, add page-flip/vblank and DRM recovery, and run both executables under the target Bionic linker. No partition installation is authorized by this work.

## Follow-up: page-flip and init service hook — 2026-11-26

- Replaced single-buffer scanout with two RGB565 dumb buffers.
- Each present renders into the back buffer, queues legacy DRM page flip with `PageFlipFlags::EVENT`, waits up to 1000 ms for the DRM fd to become readable, consumes DRM events, checks for matching CRTC `PageFlip` completion, then swaps the active buffer.
- The page-flip sequence is retained for diagnostics. This is vblank-synchronized legacy page flipping, not DRM atomic KMS. Driver support still needs physical verification; timeout/event mismatch is surfaced as an error.
- Clipboard daemon now optionally accepts `QEAF_CLIPBOARD_GID` and applies that group to the socket while preserving mode 0660. PID 1 must set this to the actual non-root application group; no guessed UID/GID is hard-coded.
- Added `system/init/start-clipboardd.sh`: PID-1 startup hook to launch daemon, wait for socket readiness, write PID file and fail if daemon exits or socket readiness times out.
- Important integration boundary: `system/init/init.c` is currently only a state-transition helper library; this repository does not expose a complete production PID-1 main loop/service supervisor to call the hook. The script is ready for invocation after `/run` is mounted, but is not yet automatically called by PID 1. Do not report PID-1 integration as complete until the actual init entrypoint is wired and tested.
- Host clipboard unit tests remain 2/2 PASS. ARM64 Bionic build of both binaries passed after page-flip implementation. Shell syntax check for the init hook passed.
- Physical Violet validation is BLOCKED: Windows `adb devices -l` and `fastboot devices` both returned no attached devices during this pass. Consequently no DRM card, connector, mode, CRTC, page-flip completion, evdev node, touch axis calibration or physical input was exercised.
- No RAM-boot image was generated or transferred; production LVGL and all device partitions remain untouched.
