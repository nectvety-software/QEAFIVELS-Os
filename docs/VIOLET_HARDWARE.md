# Violet hardware & boot-chain research (Stage 1)

Target: Xiaomi Redmi Note 7 Pro (`violet`). This document records only what
was measured on this device or read from dumps in
`D:\QeafivelsOfficial\QeafdropOs\out\reference\stock-violet\`. Anything not
verified is listed under **Open questions** — never assume it.

## 1. SoC identity — DISCREPANCY, unresolved

- The product spec says Snapdragon 675 (SM6150, Adreno 612).
- `QeafdropOs/homo/platform/violet/README.md` claims SDM660; the running
  vendor kernel self-identifies as msm-4.14 (`4.14.215-azure-Pixel+`),
  `getprop` returned `ro.product.device=violet`.
- Not yet verified from the device itself (`/proc/cpuinfo`, DTB
  `compatible`, cache geometry). **Action:** read them through a rescue
  shell verb (`kmsg` already dumps the ring; add a `cpu` verb or read
  `/sys/devices/system/cpu/`) before writing any SoC-specific driver.

## 2. Verified boot chain (from the partition dump and live behavior)

| Fact | Evidence |
|---|---|
| eMMC UFS-class storage exposed as `mmcblk0`, 122 142 720 KiB | `partitions.txt` |
| `boot` = p76, `recovery` = p77 (67 108 864 B = 64 MiB) | `by-name.txt` (p76/p77), p77 write history |
| Boot image header is **not AOSP field order**: kernel_size@0x08, kernel_addr@0x0C, ramdisk_size@0x10, cmdline@0x40 | `mk_bootimg_violet.py` measurements on the stock dump |
| Stock boot.img has **no ramdisk**; kernel payload == p76 bytes 4096..17510399 | same script |
| The vendor loader accepts our repacked images (kernel + our ramdisk) unchanged | live: every flash/reboot cycle booted our initramfs |
| `QEAFVND!` magic is a format token kept for tooling, not a runtime | `image name neutralisation script`, `validate_bootimg.py` |
| Secure boot / AVB: `vbmeta.img` exists in the dump; bootloader accepted our unsigned ramdisk — anti-rollback state not characterized | `vbmeta.img` present; empirical flashes succeeded |

Implication for Layer 1/2: we currently have **no path to replace XBL/ABL**
(risk of hard brick, and DRAM init lives in XBL). The verified working
transport is: repack our own kernel+ramdisk into the boot image header the
ABL already parses. A QEAFIVELS kernel can ride this transport while the
bootloader question stays open.

## 3. Verified hardware facts (measured by running code on the device)

| Component | Verified fact | Evidence |
|---|---|---|
| Display | DSI panel `dsi_nt36672a_shenchao_vid_display`, native 1080x2340, mode `1080x2340x60x183257`; msm_drm with **no fbdev** (`CONFIG_DRM_FBDEV_EMULATION not set`) — only `/dev/dri/card0` | `HOMOLOG.TXT` drm lines; kernel ring |
| DRM scanout | GETRESOURCES/GETCONNECTOR/CREATE_DUMB/ADDFB/SETCRTC all work from a plain aarch64 process; dumb buffer pitch 4352 B (1088 px); DIRTYFB unsupported → continuous scanout | same, plus `drm: verified - crtc 107 scans our fb` |
| USB | dwc3 UDC at `a600000.dwc3`; configfs `f_mass_storage` binds and a host sees a 32 MiB FAT LUN (`QEAFRESCUE`); UDC can register **after** first bind attempt → retry loop required | `HOMOLOG.TXT`, host `Get-Disk` |
| Touch | an evdev node exists (`vio_find_touch_device`); **not yet driven** by our code | code path exists, no evidence yet |
| Backlight | `/sys/class/backlight/backlight` accepts 85% | `HOMOLOG.TXT` |
| RTC | `qcom,qpnp-rtc` present; system clock was 1970-epoch on cold boot | kernel ring |
| Buttons | `gpio-keys` input device registered | kernel ring |
| Audio | tas2563 speaker amp on i2c 3-004c, "device not powered"; no soundcards | kernel ring |
| Wi-Fi | module `wlan` + `cfg80211` loaded; p86/p82/p74 mounted read-only and 273 firmware links made, but `wlan0` absent; booting the modem PIL (no vendor service layer beside it) resets the SoC in ~40 s, so it is opt-in | rescue `wifi state` / `diag` |
| Known noisy i2c | `i2c_geni 88c000.i2c: i2c error :-107` repeats | kernel ring |

### 3.1 USB modes: only two are usable (no PTP responder)

The stock kernel exposes an `f_mass_storage` gadget function and functionfs,
but there is **no PTP/camera gadget function and no PTP responder process** in
this image, so a photo session cannot be started at all. Exactly two modes are
therefore usable, and the shell offers only these:

| Mode | What the host sees |
|---|---|
| Charge only (`USB_MODE_CHARGE`) | nothing - the UDC is released and the phone only draws current |
| File transfer (`USB_MODE_MTP`) | the 32 MiB `QEAFRESCUE` FAT LUN |

- **Do not add a "photo transfer" choice to the shell.** It cannot be honoured.
  The sheet in `platform/violet/ui/vqeaf_ui.c` offers the two rows above plus one
  muted note saying photo transfer is unsupported; `vqeaf_ui.h` has no photo mode
  constant, and the Settings row reports only charge/file.
- `vio_lun_set_mode()` still accepts `USB_MODE_PTP` on purpose, for the platform
  contract: it converts the request to `USB_MODE_MTP` and logs `usb: PTP
  requested - no PTP responder in this image; serving the QEAFRESCUE drive
  instead`. The rescue verb `usb ptp` therefore still succeeds: `status` reports
  `usb_mode=mtp` with the drive visible, and nothing silently claims to be a
  camera.
- Boot binds the drive, so the reported mode after boot is file transfer. A
  deliberate charge-only choice releases the UDC; it is reversible without a
  reboot from Cài đặt › USB (Settings), by unplugging and plugging the cable
  again (the rising edge re-binds the drive), and by a reboot.

### 3.2 Why every boot used to reset after ~40 s: the modem, not a watchdog

The phone came up, answered on QEAFRESCUE, then rebooted 42-44 s into every
boot. The p77 journal counted `boots=476` while `CRASH.TXT` and the previous
boot record stayed empty, so no userspace fault was ever caught.

Evidence, in the order it was collected:

- `boots=` grew by one per boot and no fault record appeared -> the reset came
  from outside PID 1, not from the UI or the rescue worker.
- The `diag` verb showed `/sys/class/watchdog` absent, no `*wdt*` module under
  `/sys/module`, and no `/vendor/lib/modules` at all: this kernel exposes no
  watchdog driver, so `/dev/watchdog` (node present, driver absent) cannot be
  the timer. Battery was 27 % and charging, SoC 37 C -> not a brownout or a
  thermal cut.
- `wifi off` closes the modem PIL fd (`/dev/subsys_modem`): with the modem
  released, the same image stayed up 598 s with zero resets. A default boot
  that never starts the modem was still climbing at 360 s.

So the reset is the modem subsystem: PIL boots `modem.mdt`, the modem runs
without the vendor service layer that normally runs beside it, and the msm
subsystem restarts the whole SoC about 40 s later. Nothing in userspace can be
blamed, and no crash record can exist because the kernel does not panic.

What the image does now:

- Boot mounts `/vendor` (p86), `/vendor/firmware_mnt` (p82) and
  `/mnt/vendor/persist` (p74) read-only and links the 273 firmware files - all
  read-only and safe - but **does not boot the modem**.
- `wifi up` is the only path that starts it, and it says that doing so may
  reset the SoC. `wifi off` releases it again. `wlan0` still does not exist.
- `platform/violet/watchdog.c` opens `/dev/watchdog` and pets it only while
  the main loop has acked within 8 s. On this kernel it logs `no watchdog
  device` and does nothing; it exists so a kernel that does expose a watchdog
  is never left unfed, and so a wedged main loop still resets the phone.

## 4. Partition map highlights (from `by-name.txt`)

Boot-critical: `xbl`/`xbl_config` (p-sector), `abl`+`ablBAK` (p41/p42),
`boot` p76, `recovery` p77, `dtbo` p60, `devcfg` p39/p40, `vbmeta`,
`devinfo` p68 (unlock/verify state), `misc` (bootloader control block).
Radio sources are separate partitions: `bluetooth` p38 (VFAT, mounted by stock
Android at `/vendor/bt_firmware`), `modem` p82 (VFAT,
`/vendor/firmware_mnt`), and `persist` p74 (ext4, per-device calibration and
addresses). `userdata` is a separate partition. The live rescue capture above
predates the new read-only p38 mount in `platform/violet`.

Full symlink table: `QeafdropOs/out/reference/stock-violet/by-name.txt`.

## 5. What this means for the 20-stage plan

- **Stage 1 (this document):** done — with two open questions (SoC ID,
  AVB/anti-rollback state).
- **Stage 2 (bootloader PoC):** the realistic first artifact is a
  QEAFIVELS "stage-2" that ABL loads from a boot image and hands control to
  — i.e. our own code replaces the *Linux kernel slot*, not XBL. True
  bootloader ownership requires XBL research and carries brick risk;
  document before touching anything.
- **Stage 3 (ARM64 kernel startup):** a freestanding aarch64 kernel entered
  through the boot-image transport, with UART (`earlycon=msm_geni_serial,
  0x880000` is already in the vendor cmdline) as the first output. The
  current `platform/violet/main_violet.c` PID 1 runs **on the stock Linux
  kernel** — it is an interim transport user and must never be presented as
  the QEAFIVELS kernel.
- Touch (stage 7) needs the raw evdev protocol dump before any driver work.
- UFS/storage (stage 8) on bare metal means driving the UFS controller
  directly — the vendor kernel's `ufs_phy_gdsc` lines hint at the topology
  but no register-level documentation is in hand yet.

## 6. Open questions (blocking, must be resolved with evidence)

1. SoC: SM6150 or SDM660? Read `/proc/cpuinfo`, DTB compatible, PMIC model.
2. Is the bootloader locked? Does AVB verify p76/p77 on this unit? (Our
   unsigned images booted — but the exact verification state is unknown.)
3. Can `misc` BCB be used for our own boot-mode protocol without breaking
   vendor-platform fallback? (QeafdropOs never writes p76/misc by policy.)
4. Touch controller identity + raw evdev axes for `nt36672a`-era panels.
5. Which XBL version is installed, and is there any documented, non-bricking
   path to run our own code before ABL?
