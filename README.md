# QEAFIVELS OS

**Independent OS status:** the x86_64 UEFI/QEMU kernel is a development reference only. The current `platform/violet` Linux-musl PID 1 image runs on the stock Linux kernel and is a legacy transport prototype, not the independent QEAFIVELS OS. A freestanding ARM64 Violet kernel has not yet been brought up. See `docs/INDEPENDENT_OS_CHARTER.md` for the canonical architecture, hardware evidence, safety policy and stage gates.
Independent native operating-system project by Qeafivels Software.

VQEAF is an independent OS. It does not use a vendor mobile-platform runtime, vendor framework, ART/Dalvik, APK-style packages, Java/Kotlin, a vendor System UI, a vendor package manager, a vendor SDK, or a vendor debug bridge.

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

The normal QEMU test proves Boot Manager -> GOP boot splash -> KERNEL.EFI -> boot protocol v3 (memory map + framebuffer) -> ExitBootServices -> paging -> native UI -> VirtIO PCI controller probe -> interrupt-driven block I/O -> MBR partition discovery -> read-only FAT16 mount -> VFS read -> AHCI/SATA probe -> second FAT16 mount at /storage -> scratch-sector write/readback/restore -> unmount/remount -> preemptive kernel and ring-3 tasks. Recovery is booted separately. The shipped display default is portrait 9:16: the Boot Manager selects a 720x1280 framebuffer (on QEMU through the Bochs VBE registers, because OVMF only advertises landscape GOP modes) and the shell fills it edge to edge. On firmware without a portrait mode or Bochs VBE it falls back to a 450 px centred 9:16 column inside the landscape mode. After boot the native shell is a smartphone UI: lock screen with CMOS time/date, an 8-tile home launcher, quick settings and About/Settings/Storage/Clock apps. Wi-Fi, Bluetooth and battery are shown as unsupported instead of inventing readings. This is still not a complete interactive OS (no touch input, compositor or app runtime).

Phone keys over the QEMU serial console: W/A/S/D move, E or Enter selects, B/Backspace goes back, Q opens quick settings, L locks, P taps power and Shift+P holds power. ESC [ A..D arrows work too.

Storage stack: `kernel/arch/x86_64/pci.c` scans PCI config space (all functions), `kernel/drivers/virtio_blk.c` drives the legacy VirtIO block queue over INTx, `kernel/drivers/ahci.c` drives SATA ports with IDENTIFY and READ/WRITE DMA EXT, and `task_storage` verifies both chains against the test disks. The shell's Storage app shows the live `/storage` mount and file content. See docs/ROADMAP.md for the requested development sequence and what remains for Recovery, Update Engine, Power & Display and Smartphone UI.

## Violet port (Redmi Note 7 Pro) — PID 1 đã boot thật (2026-11-10)

Cây này có một cổng violet độc lập nằm trong `platform/violet/`: PID 1 tĩnh
aarch64-musl chạy trên **stock kernel** của máy, dựng LUN cứu hộ QEAFRESCUE,
trả lời `QEAF.CMD`, và render splash XRGB8888 720x1560 lên panel 1080x2340
qua DRM/KMS. Không dùng nền tảng di động của nhà sản xuất, không AOSP — cùng nguyên tắc với bản x86_64.

Pipeline (mượn toolchain Zig và kernel tham chiếu từ
`D:\QeafivelsOfficial\QeafdropOs`, thiết lập qua `QEAFDROPOS` nếu khác):

    python scripts/build_violet.py      # zig cc -target aarch64-linux-musl -> build/violet/vqeaf-init
    python scripts/package_violet.py    # stage -> cpio.gz -> boot image -> gates

`package_violet.py` xuất `dist/boot-vqeaf-violet.img` và **tự chạy hai gate**
của QeafdropOs trước khi kết thúc: `validate_bootimg.py` (cấu trúc boot image)
và `audit_rescue_channel.py` (kênh QEAF.CMD + marker configfs/UDC + driver
panel/gadget phải nằm trong chính ELF `/init`). Ảnh bị từ chối không bao giờ
tới được tay người nạp.

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

## Deployment boundary

Đây là một dự án x86_64 UEFI **tham chiếu** chỉ được kiểm chứng trong QEMU,
cộng một cổng violet ở `platform/violet/` nạp đúng qua p77/recovery với đầy
đủ gate và đồng ý. Không có signed update hay rollback. Ghi ảnh x86_64 lên
thiết bị vật lý hoặc nạp ảnh không qua gate lên p77 sẽ phá hủy phân vùng;
cả hai đều bị các script từ chối.

Build/test/capture scripts write only inside this project and QEMU virtual machines. They never flash or write physical devices. Any deployment to real hardware requires an explicit target, a full hardware port, and a separate authorisation step.

See docs/ARCHITECTURE.md, docs/BOOT_PROTOCOL.md, docs/ROADMAP.md and docs/STATUS.md for architecture, the v3 handoff, next phases and exact limitations.
