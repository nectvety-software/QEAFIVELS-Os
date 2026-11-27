# QEAFIVELS OS

**Linux-core status:** QEAFIVELS uses Linux as the kernel core for the whole OS. The Violet production path runs the vendor Linux `4.14.215-azure-Pixel+` kernel with QEAF-owned PID 1, services, framework, UI, QEAPP and rescue. The separate x86_64/QEMU baseline now boots Linux 4.14.215, QEAF PID 1/services and a native Puzzle `.qeapp`; the existing x86_64 UEFI and AArch64 freestanding kernels remain reference tracks. See `docs/ARCHITECTURE.md` and `docs/INDEPENDENT_OS_CHARTER.md` for ownership, evidence, safety and migration gates.
Independent native userspace and application ecosystem by Qeafivels Software.

QEAFIVELS does not use an Android/AOSP/ART runtime, vendor framework, APK-style packages, Java/Kotlin, a vendor System UI, a vendor package manager, a vendor SDK or a vendor debug bridge. Linux is the explicitly approved kernel core; it is not presented as a QEAF-written kernel.

## Create a QEAPP application

```text
python sdk/cli/qeaf.py create examples/my-app --id org.example.myapp --package-name my-app
python sdk/cli/qeaf.py build examples/my-app --config all
python -m unittest discover -s examples/my-app/tests -v
```

The generated project includes `src/`, `libs/`, `assets/`, `packaging/`,
`tests/`, `compiler/`, and `build.py`. Edit `packaging/package.json` to name
outputs in `build/releases/` and `build/debug/` without changing the manifest
app identity. The compiler validates declarative `app`/`print` scripts and
creates signed development packages; it is not a native-code compiler.
See the [SDK guide](sdk/cli/README.md),
[example structure](examples/hello-qeapp/README.md), and
[QEAPP format](docs/QEAPP_FORMAT.md).

## Build and test on Windows
    python scripts/doctor.py
    python scripts/build.py
    python scripts/test.py

Capture what the screen actually looks like (real QEMU framebuffer screenshots):

    python scripts/capture_screen.py
    # -> build/screens/splash.png and build/screens/ui.png
    python scripts/test_recovery_ui.py
    # -> build/screens/recovery_menu.png
    python scripts/test_input_power.py
    # -> phone_lock/home/quick/settings/storage/clock/about.png,
    #    screen_asleep.png, screen_awake.png, update_blocked.png, power_menu.png

Every capture is the live 720x1280 (exact 9:16) portrait framebuffer; the
scripts fail if QEMU comes back with any other geometry.

Artifacts:
- dist/vqeaf-x86_64-uefi.img — normal UEFI reference boot media.
- dist/vqeaf-x86_64-recovery.img — standalone Recovery boot media.
- build/virtio-test.img — deterministic MBR + FAT16 test disk attached as a second QEMU VirtIO drive.
- build/ahci-test.img — MBR + FAT16 test disk attached to the emulated SATA/AHCI controller (`ide-hd,bus=ide.1`).
- build/linux-x86_64/ — separate upstream Linux 4.14.215 `bzImage`, effective config, `System.map`, static QEAF init/services/runner, deterministic initramfs, Puzzle package, boot log and provenance manifest. These files are not Violet or freestanding UEFI artifacts.

## Linux x86_64/QEMU QEAF and QEAPP gate (2026-11-25)

```text
python scripts/build_linux_x86_64.py --skip-kernel --timeout 60
python scripts/build_linux_x86_64.py --verify-only
python -m unittest tests.test_linux_x86_64_baseline
```

The gate boots Linux 4.14.215 with QEAF PID 1, starts `package-store` and
`qeapp-preflight`, verifies the real x86_64 Puzzle `.qeapp`, launches it in a
forked uid/gid 20000 sandbox with `no_new_privs` and seccomp, checks the full
lifecycle/framebuffer evidence, stops services and powers off cleanly. The
latest baseline tests are 21/21 PASS and the focused Linux/QEAPP regression
selection is 66/66 PASS. Three negative QEMU cases also fail closed as expected:
missing services, missing package and tampered package never reach service
READY or QEAPP RUN and power off cleanly.

This is a headless emulator baseline, not a complete UI, rescue, persistence or
update system and not physical x86_64/Violet evidence. The QEAPP arena remains
RWX, rlimit failures are unchecked, namespaces/chroot/device cgroups/LSM policy
are absent, and the embedded development HMAC is not publisher authentication.
Linux 4.14.215 still requires a security backport/CVE review.

The normal QEMU test proves Boot Manager -> GOP boot splash -> KERNEL.EFI -> boot protocol v3 (memory map + framebuffer) -> ExitBootServices -> paging -> native UI -> VirtIO PCI controller probe -> interrupt-driven block I/O -> MBR partition discovery -> read-only FAT16 mount -> VFS read -> AHCI/SATA probe -> second FAT16 mount at /storage -> scratch-sector write/readback/restore -> unmount/remount -> preemptive kernel and ring-3 tasks. Recovery is booted separately. The shipped display default is portrait 9:16: the Boot Manager selects a 720x1280 framebuffer (on QEMU through the Bochs VBE registers, because OVMF only advertises landscape GOP modes) and the shell fills it edge to edge. On firmware without a portrait mode or Bochs VBE it falls back to a 450 px centred 9:16 column inside the landscape mode. After boot the native shell is a smartphone UI: lock screen with CMOS time/date, an 8-tile home launcher, quick settings and About/Settings/Storage/Clock apps. Wi-Fi, Bluetooth and battery are shown as unsupported instead of inventing readings. That freestanding UEFI reference image is still not a complete interactive OS (no touch input, compositor or app runtime).

Phone keys over the QEMU serial console: W/A/S/D move, E or Enter selects, B/Backspace goes back, Q opens quick settings, L locks, P taps power and Shift+P holds power. ESC [ A..D arrows work too.

Storage stack: `kernel/arch/x86_64/pci.c` scans PCI config space (all functions), `kernel/drivers/virtio_blk.c` drives the legacy VirtIO block queue over INTx, `kernel/drivers/ahci.c` drives SATA ports with IDENTIFY and READ/WRITE DMA EXT, and `task_storage` verifies both chains against the test disks. The shell's Storage app shows the live `/storage` mount and file content. See docs/ROADMAP.md for the requested development sequence and what remains for Recovery, Update Engine, Power & Display and Smartphone UI.

## Violet Linux-core path (Redmi Note 7 Pro) — PID 1 đã boot thật (2026-11-10)

Đường chạy production nằm trong `platform/violet/`: PID 1 tĩnh aarch64-musl
chạy trên **vendor Linux 4.14.215** của máy, dựng LUN cứu hộ QEAFRESCUE,
trả lời `QEAF.CMD`, và render splash XRGB8888 720x1560 lên panel 1080x2340
qua DRM/KMS. Không dùng nền tảng di động của nhà sản xuất, không AOSP — cùng nguyên tắc với bản x86_64.

Pipeline (mượn toolchain Zig và kernel tham chiếu từ
`D:\QeafivelsOfficial\QeafdropOs`, thiết lập qua `QEAFDROPOS` nếu khác):

    python scripts/build_violet.py      # zig cc -target aarch64-linux-musl -> build/violet/vqeaf-init
    python scripts/package_violet.py    # stage -> cpio.gz -> boot image -> gates

`package_violet.py` xuất `dist/boot-vqeaf-violet.img`, kiểm tra kernel payload
trùng byte-for-byte với stock reference, xác nhận `/init` static AArch64 và
**tự chạy hai gate** của QeafdropOs: `validate_bootimg.py` (cấu trúc boot
image) và `audit_rescue_channel.py` (kênh QEAF.CMD + marker configfs/UDC +
driver panel/gadget phải nằm trong chính ELF `/init`). Sau khi tất cả PASS,
script ghi `dist/boot-vqeaf-violet.manifest.json`. Kiểm tra artifact hiện có
bằng `python scripts/package_violet.py --verify-only`. Ảnh bị từ chối không
bao giờ tới được tay người nạp.

Nạp và bằng chứng bắt buộc (chỉ p77/recovery, cần đồng ý rõ ràng):

- Ảnh đi qua `scripts/prepare_rescue_flash.py` của QeafdropOs (`--commit`
  là sự đồng ý), hoặc `restore_p77.ps1 -Execute -ConfirmPartitionWrite`
  khi máy đang ở fastboot.
- Hai bằng chứng sau flash: `flash: written and verified byte for byte`
  trong RESPONSE.TXT và `lun: verified - recovery and BOOTIMG.BIN agree byte
  for byte over N bytes` trong QEAFBOOT.LOG — đọc bằng raw sector, không đọc
  view mount.
- Bằng chứng boot: `HOMOLOG.TXT` phải có `boot: VQEAF-OS ... init on violet
  (PID 1)`, `drm: verified - crtc ... scans our fb`, và `ui: splash 720x1560`.

Bằng chứng lượt đầu (2026-11-10, ảnh 18 026 496 B, sha256 `1961a332…b2519`):
flash verified, PID 1 boot, `usb: mass storage bound to a600000.dwc3`,
`drm: verified - crtc 107 scans our fb 188 at 1080x2340`,
`ui: splash 720x1560 (portrait)`; SCREEN.RAW chụp từ kính xác nhận splash.
Reports: `QeafdropOs/out/report-vqeaf-violet-20261110/` và
`QeafdropOs/out/report-vqeaf-reboot/`.

LVGL phone shell đã chạy thật trên máy (Lock/Home/Apps/Control Center/Settings/
Recents + các trang nội dung), touch, cử chỉ cạnh, PIN màn hình khóa, phím ảo,
chụp màn hình (`shot` / Power+Volume), bàn phím **emoji thật** và QEAPP runtime.
Các verb rescue hữu ích: `status`, `wifi up|svc|log|sup|gnss`, `snap`, `shot`,
`pstore`. Xem `docs/PHONE_UI.md`, `docs/STATUS.md`,
`docs/platforms/VIOLET_WIFI_BRINGUP.md` và skill `.agents/skills/violet-port/`.

**Wi-Fi/GNSS vẫn BLOCKED, không giả:** kernel stock 4.14.215 đã built-in driver
wlan/bluetooth nhưng dịch vụ WLFW QMI `0x45` không bao giờ được modem publish,
nên `wlan0` absent. cnss-daemon đã chạy (sau khi mount binderfs `/dev/binder`),
nhưng im lặng; GNSS không có device node. Toàn bộ HAL/.so thật đã trích từ ROM
gốc (`/opt/bionic-cnss` trong WSL). Không mock QMI, không báo kết nối giả.

## MRE-Os AArch64 integration

The standalone MRE-Os source is preserved under `integrations/MRE-Os/` as
reference material. Its freestanding FDT/MMU/GIC/timer and Violet bootstrap
work is not the production Linux driver path. Linux owns production hardware;
QEAF services consume Linux device interfaces. See
`docs/platforms/VIOLET_NATIVE_BOOT_INTEGRATION.md` and
`integrations/MRE-Os/INTEGRATION.md` for the retained reference track.

## Deployment boundary

Dự án có đường Violet Linux-core ở `platform/violet/`, đường Linux x86_64/QEMU
riêng ở `platform/linux-x86_64/`, và các artifact x86_64 UEFI/AArch64
freestanding **tham chiếu**. Không có signed update hay rollback hoàn chỉnh.
Ghi ảnh x86_64
lên thiết bị vật lý hoặc nạp ảnh không qua gate lên p77 sẽ phá hủy phân vùng;
cả hai đều bị các script từ chối.

Build/test/capture scripts write only inside this project and QEMU virtual machines. They never flash or write physical devices. Any deployment to real hardware requires an explicit target, a full hardware port, and a separate authorisation step.

See docs/ARCHITECTURE.md, docs/BOOT_PROTOCOL.md, docs/ROADMAP.md and docs/STATUS.md for architecture, the v3 handoff, next phases and exact limitations.
