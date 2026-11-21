# QEAPP launch pipeline — vendor mobile-platform overview vs. what QEAF really does

The reference overview for a vendor mobile-platform app launch is:

```
Bấm icon → Launcher → ActivityManagerService
                          ↓
                   Zygote fork()
                          ↓
               Tiến trình app (UID riêng)
                          ↓
        ART chạy mã DEX/OAT + thư viện native
                          ↓
           Vẽ giao diện qua Binder → SurfaceFlinger
```

QEAF keeps the same *shape* (a tap never runs app code inside the launcher), but
each stage is a much smaller mechanism. This page maps the diagram onto the code
and records the values measured on the reference handset (Redmi Note 7 Pro,
`violet`, arm64).

## Stage by stage

| Diagram stage | QEAF implementation | Evidence |
| --- | --- | --- |
| Tap icon → Launcher | Grid "Đã cài" in the Apps page; the tile's `qeapp_open_cb` (`platform/violet/ui/vqeaf_pages.c`) calls `vqeaf_ui_run_app(index)` for an arm64 package. | Screenshot of the tile and of the page it opens: `build/screens/device/pipeline-apps.png`, `pipeline-launch.png` |
| → ActivityManagerService | The shell itself is PID 1 (`vqeaf-init`); the "activity manager" is `vqeaf_ui_show()` for pages plus the QEAPP registry `vqeaf_qeapp` (install / select / launch). The guarded launcher `vqeaf_qeapp_device_launch()` is the AMS/ANR part: a hard 5 s deadline, then `SIGKILL`, and the shell keeps running. | `VQEAF_QEAPP_DEVICE_TIMEOUT_MS 5000` in `vqeaf_qeapp_device.h` |
| → `Zygote fork()` | Plain `fork()` in `vqeaf_qeapp_device.c`. The child's stdout/stderr go into a pipe so the shell can read the run report; the XRGB8888 canvas is `mmap(MAP_SHARED)` **before** the fork, so pixels survive a clean exit and a crash cannot corrupt them. | `app pid 494 · shell pid 1` printed on the native page by `vqeaf_qeapp_device_process()` |
| → App process (own UID) | A real, separate process (child of PID 1), but **no separate UID**: all of QEAF userspace is UID 0. Isolation comes from the frozen API table — the module gets no syscall, no file descriptor and no kernel pointer, only `draw_text`, `request_permission`, `read_file`, `free`. | `uid 0` in the same measured line; API table printed by the app itself (`api table size`, `abi_version 4`) |
| → ART runs DEX/OAT + native libs | No DEX, no ART, no dynamic linker. The signed `.qeapp` is unsealed, the `.qen` arm64 module is validated, its code/rodata/data are copied into a 4096-byte aligned 2 MiB `PROT_EXEC` arena, BSS zeroed, cache flushed, then the lifecycle exports are resolved by name. | `lifecycle: create start pause resume stop destroy`; `qeapp validate` → "QEAPP package valid" |
| → Draws through Binder → SurfaceFlinger | No Binder, no SurfaceFlinger. The module rasterises into the shared canvas through its API (`draw_text`, `blit_pixels`); when the child exits cleanly the shell composites that canvas as an LVGL image at 1:1 over the whole screen — the app's frame *is* the app's UI, with only a translucent back chip on top. | `hash=0x10dc0e21` (FNV-1a over all 720×1560×4 bytes) reported on the app's detail page, matching the QEMU arm64 gate value |

## Measured run (device, 2026‑11‑14)

- Image: `dist/boot-vqeaf-violet.img`, sha256 `5388f742…bf5d68`, 20 205 568 B.
- Flash proof: `flash: written and verified byte for byte` and
  `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over 20205568 bytes`.
- Install: `install TERMINAL.QEAPP` → `apps: 14 installed`,
  `- Terminal (native) /TERMINAL.QEAPP`.
- Launch by tapping the icon: the Terminal UI opens directly, full screen and
  1:1 (`build/screens/device/app-ui2.png`), with a back chip that returns to
  Apps (`app-ui-back.png`). The launch facts (`lifecycle: create start pause
  resume stop destroy`, `hash=0x10dc0e21`, `app pid 494 · shell pid 1 · uid 0`)
  moved to the app's detail page instead of sitting on top of the app's pixels.

## Caveats worth keeping in mind

- The `uid 0` line above is the pre-sandbox measurement (2026-11-14). Since the
  process sandbox landed, the child drops to uid/gid 20000, keeps no inherited
  descriptor and runs under a seccomp allowlist; see
  `docs/QEAPP_SECURITY_PHASE_IMPLEMENTATION_2026-11-20.md`. Treat a QEAPP as
  owner-signed code with a syscall filter, not as a fully isolated third-party
  app: there is still no mount namespace or revocation channel.
- The launch is synchronous on the LVGL thread: the UI freezes for the module's
  own run, capped at 5 s. The vendor reference AMS is asynchronous; QEAF has no launcher
  task queue yet.
- There is one ABI, arm64 only; no JIT/AOT layer and no shared native libraries.
  A module must be fully static inside its `.qen`.
- Rendering is one shared canvas per launch (no per-window surfaces, no vsync
  sync with the compositor beyond the shell's own frame pacing).
- The info sheet (uninstall, detail page) is reached by **holding** the icon.
  From the host, `hold <x> <y> [ms]` exercises the same path; a verified run
  opened the sheet and then the detail page through its `Thông tin` button
  (`build/screens/device/final-sheet.png`, `final-info.png`).
- The status bar follows the app's own pixels: if the frame leaves the top
  56-pixel band blank it is drawn over the frame at 20 % opacity (a phone keeps
  its clock there), and it stays hidden when the frame paints in that band.
  Terminal paints from y=21, so its page keeps the bar hidden rather than
  covering the `Terminal 1.0` line.
- The sheet lives on `lv_layer_top()`: the scrim and the card are siblings, and
  its wrapped labels are pinned to one line (`LV_LABEL_LONG_MODE_DOTS`) so the
  layout is final before the first paint. Without that, the painted buttons sat
  one layout above their hit boxes and every tap landed on the scrim.
