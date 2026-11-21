# Skill: Violet port (Redmi Note 7 Pro)

Build, package and flash the VQEAF-OS aarch64 PID 1 from this tree onto the
Redmi Note 7 Pro (codename `violet`), and verify it with the same evidence
rules the x86_64 reference uses. Read `.agents/AGENTS.md` first.

## What exists

- `platform/violet/` — the port: `channel/` (rescue channel, FAT16 LUN,
  sysio/log, HAL headers), `drm_kms.c` (DRM/KMS XRGB8888 scanout),
  `panel_scale.c` (720x1560/1560x720 canvas onto 1080x2340), `usb_lun.c`
  (configfs `f_mass_storage`), `devnodes.c`, `display_hal.c`,
  `violet_input.c`, `violet_power.c`, `main_violet.c` (PID 1), `splash.c`.
- `scripts/build_violet.py` — Zig 0.13 (`-target aarch64-linux-musl`,
  `-static`), links `build/violet/vqeaf-init`.
- `scripts/package_violet.py` — stages `/init`, reuses QeafdropOs
  `mk_initramfs.py` + `mk_bootimg_violet.py` (stock kernel, patched header),
  runs the gates, writes `dist/boot-vqeaf-violet.img`.
- QeafdropOs checkout (default `D:\QeafivelsOfficial\QeafdropOs`, override
  with `QEAFDROPOS`) supplies the Zig toolchain, the stock kernel dump and
  the flash/verify scripts.

## Build

```
python scripts/build_violet.py            # incremental
python scripts/build_violet.py --clean    # from scratch
python scripts/package_violet.py          # image + gates (implies build)
python scripts/package_violet.py --skip-build
```

Output: `dist/boot-vqeaf-violet.img`. Two gates run at the end and must PASS
before anything else happens:

1. `QeafdropOs/scripts/validate_bootimg.py` — boot image structure.
2. `QeafdropOs/scripts/audit_rescue_channel.py` — rescue channel files plus
   `gadget_configfs/vendor/function/UDC` markers, and `init_panel` /
   `init_gadget` markers counted **inside the ELF that `/init` is**, not
   elsewhere in the ramdisk. A marker anywhere else does not count.

## Flashing — consent rules

- Only `p77` (recovery) is ever written. Never p76/boot, never userdata,
  never misc without an explicit user request.
- Two allowed paths, both consent-gated:
  1. Device running the rescue OS with `F:` (QEAFRESCUE) mounted:
     `QeafdropOs/scripts/prepare_rescue_flash.py --image
     <img> --volume F: --commit` (dry run first, `--commit` is consent).
  2. Device in fastboot: `QeafdropOs/scripts/restore_p77.ps1 -Image <img>
     -Execute -ConfirmPartitionWrite`. `-Execute` alone is refused.
- Never flash an image that failed a gate, and never flash the x86_64
  `dist/vqeaf-x86_64-*.img` files to a phone.

## Required evidence (no claim without it)

After staging through `F:`: flush the volume (`Write-VolumeCache F` or
`sync`), wait ~10 s, read back with
`QeafdropOs/scripts/read_rescue_raw.py --drive F: --out <dir> --files ...`
by **raw sector** (a mounted view goes stale). Require:

- `RESPONSE.TXT`: `flash: written and verified byte for byte`
- `QEAFBOOT.LOG`: `lun: verified - recovery and BOOTIMG.BIN agree byte for
  byte over N bytes` (N = image size)

After a reboot into the new image (`send_rescue_cmd.py --volume F: --cmd
reboot`), require from `HOMOLOG.TXT`:

- `boot: VQEAF-OS <ver> init on violet (PID 1)`
- `usb: mass storage bound to a600000.dwc3` (a first `No such device` line
  is the UDC not yet registered; the retry loop binds it)
- `drm: verified - crtc <n> scans our fb <n> at 1080x2340`
- `ui: splash 720x1560 (portrait)`

Visual proof: `snap` verb writes `SCREEN.RAW` (240x520 RGB565, no MAP
header); convert with a plain RGB565 unpack, then check the pixels.

Recovery if the phone wedges (enumerates as `Generic Mass-Storage / No
Media`, no adb/fastboot): hold **Volume Down + Power ~8 s**, replug USB,
wait for `fastboot devices`, then restore via
`restore_p77.ps1 -Image out/boot-qeafdropos-violet.img -Execute
-ConfirmPartitionWrite` from the QeafdropOs tree.

## Current working workflow (device `ca1af25d`)

The device is attached over the QEAFRESCUE `F:` volume (auto-detected). The
fastest loop uses the project helpers rather than the QeafdropOs scripts:

```
python scripts/build_violet.py                    # aarch64-musl PID 1
python scripts/package_violet.py                  # image + gates -> dist/boot-vqeaf-violet.img
python scripts/device_check.py --volume F: --image dist/boot-vqeaf-violet.img --flash \
    --steps "cmd status;expect resp QEAFIVELS"    # flash p77/recovery (consent already implied by --flash)
python temp\probe_device.py --cmd status          # read-only rescue verb
python temp\probe_device.py --cmd snap            # SCREEN.RAW -> decode with scripts/device_check.py:raw_to_png
```

If the device is in Android instead of QEAFRESCUE: `adb reboot bootloader`,
then `fastboot flash recovery dist/boot-vqeaf-violet.img` (still only p77),
then `fastboot reboot recovery`. Never write p76/boot, userdata or misc.
If the device wedges at stage `splash` with no rescue answers, ask the user
to hold Power 10–15 s; do not keep retrying a flash that will time out.

## What actually works on violet (verified, 2026-11-21)

- Interactive LVGL shell (Lock/Home/Apps/Control/Settings/Recents + content
  pages), touch, gestures including edge/swipe arbitration, PIN lock, virtual
  navigation keys, screenshot via `shot`/Power+Volume, **emoji soft keyboard**
  (real emoji font from Segoe UI Emoji), QEAPP runtime, USB MTP/rescue.
- Kernel: stock `4.14.215-azure-Pixel+` (monolithic). wlan/cfg80211/bluetooth
  drivers are built in; `/dev/wlan` is major 500. Wi-Fi is **NOT** usable:
  WLFW QMI `0x45` is never published, `wlan0` absent. cnss-daemon now runs
  thanks to binderfs (`/dev/binderfs`, `/dev/binder -> .../binder`); a liblog
  shim (`platform/violet/hosttools/src/liblog-shim/`) redirects Android logcat
  calls to stderr but the stock daemon still emits no debug. GNSS has no
  kernel device node; GNSS HAL and wpa_supplicant binaries run but abort at
  binder/changed state. All NEEDED `.so` came from the **real ROM dump**
  (`/opt/bionic-cnss` in WSL, APEX under `/system/apex`). No mock QMI, no fake
  hardware. See `docs/platforms/VIOLET_WIFI_BRINGUP.md`.
- Session-only settings on violet (no prefs partition); theme/font/PIN/nav
  mode are RAM-only and reset on reboot.

## Known constraints

- `/` is the initramfs in RAM: everything not packed into it is lost on
  reboot. A durable prefs backend is not implemented; settings are session-only.
- The rescue worker logs boot stages only after it starts, so early stage
  changes (`lun`, `dri-wait`) may be absent from `QEAFBOOT.LOG`.
- `DEVINFO.TXT` publishes only values the OS actually knows.
- Wi-Fi, GNSS and hardware biometrics remain BLOCKED at the firmware/service
  layer; report `BLOCKED_FIRMWARE`/`UNSUPPORTED`, never a fake connected state.
- A `liblog.so` shim replaces the stock Bionic liblog in the ramdisk (the real
  one is kept as `liblog.real.so`) so stderr debug is capturable; this changes
  only where log text goes, not QMI/service behaviour.
