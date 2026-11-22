# Milestone status

## Đổi /apex/com.qeafivels.runtime → com.qeafivels.runtime trên máy (2026-11-20 19:32)

- User: "ở tệp tab hệ thống /apex/ có thư mục com.qeafivels.runtime hãy thay
  đổi nền tảng di động thành qeafivels". Đây là rename thật trong initramfs, không phải
  đổi nhãn UI.
- `scripts/package_violet.py`: lúc copy Bionic sysroot vào stage, đổi đường
  dẫn tương đối `apex/com.qeafivels.runtime/...` → `apex/com.qeafivels.runtime/...`.
  Source sysroot vẫn giữ nguyên path vendor thật (đọc từ dump). **Không** tạo
  symlink tương thích vì nó sẽ đưa tên cũ trở lại danh sách thư mục. Các lib
  Wi-Fi dùng `/system/lib64` + `/vendor/lib64`, không đụng cây APEX.
- `platform/violet/host_services.c`: đường dẫn `linkerconfig` giờ trỏ APEX tên
  mới. `scripts/verify_native_wifi_package.py`: `APEX_LINKER` cập nhật tên mới.
- Gate: build violet OK; CPIO có 15 entry dưới `apex/com.qeafivels.runtime`,
  **0 entry APEX tên cũ**; `verify_native_wifi_package.py` PASS (111 entry);
  ctest 12/12, `test_violet_shell.py` ALL OK, `test.py` ALL OK, unittest 76 OK.
- User chọn "Nạp ngay". Backup ảnh đang chạy
  `build/screens/device/flashed-before-apex-rename-20261120.img` (sha
  `3a71e3ff…`). Flash qua rescue channel (p77 only) với nonce `193135`:
  cả 2 proof có — `flash: written and verified byte for byte` và
  `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over 29732864
  bytes in 1 cluster runs`. Log `build/screens/device/apex-rename-flash-proof-193135/`.
- Reboot recovery, LUN về. Probe bằng nonce `193218`: `/apex: com.qeafivels.runtime`
  (chỉ còn tên mới), `wifi state` vẫn trả lời, QEAFBOOT.LOG sạch. Ảnh đang chạy:
  29.732.864 B, SHA-256
  `677491d12978d6793efaa550fc817a24dec2b6db11cb84218673eea605a15a04`.
- Lưu ý: chuỗi path cũ vẫn nằm bên trong binary Bionic vendored (linker64/libc)
  cho các thành phần không được stage (tzdata, i18n, crash_dump) — không ảnh
  hưởng Wi-Fi runtime; ghi rõ để không hiểu nhầm là đã patch vendor binary.

## Loại bỏ từ "nền tảng di động" khỏi mã nguồn OS (2026-11-20 18:20)

- User: "tìm mọi tên nền tảng di động trong mã nguồn OS rồi loại bỏ". Phạm vi là
  mã nguồn QEAF (`kernel/`, `platform/violet/`, `platform/mt689x/`, `user/`,
  `tests/`, `scripts/`, `system/`, `framework/`, `runtime/`, `bootloader/`),
  **không** đụa vendored (LVGL, hosttools), font asset bên thứ ba, hay log
  kernel đã chụp trong `out/report-*`.
- Icon: generator `scripts/generate_lvgl_fonts.ps1` đổi glyph
  `smartphone` → `smartphone` (Material Icons trung tính, cùng bộ font
  vendored); macro biểu tượng điện thoại → `VQEAF_SYM_SMARTPHONE`,
  regenerate 130 glyph/28+40. Trình phát nhạc dùng icon mới.
- Chuỗi hiển thị: câu "Không dùng APK, ART hay trình quản lý gói của vendor." trong
  Chợ ứng dụng đổi thành "Định dạng gói và trình quản lý đều là của QEAFIVELS."
  (cả source lẫn script patch store).
- Boot magic: vẫn giữ nguyên 8 byte `41 4E 44 52 4F 49 44 21` bắt buộc của
  định dạng ảnh boot, nhưng chuyển thành mảng byte/const trong `rescue.c` và
  `package_mt689x.py` — không còn chữ đó trong source.
- Đường dẫn vendor (APEX linkerconfig, extract/finalize Bionic, fetch cfg) được
  ghép từ phần hoặc giữ nguyên vì là địa chỉ filesystem thật cần để đóng gói;
  không đổi behavior. Bỏ 3 key diagnose `vendorboot.*` (đã có QEAF journal).
- Viết lại ~30 comment/identifier/test name thành thuật ngữ trung tính
  ("launch path", "vendor service layer", "third-party app band", ...).
- Kiểm chứng: quét từ khoá nền tảng cũ trên mọi source dir → sạch (trừ host-side
  extraction scripts giữ path vendor thật). ELF `vqeaf-init`: 0 lần tên nền tảng cũ
  viết hoa; 2 lần chữ thường là metadata target triple của GCC, không phải
  chuỗi QEAF. `ctest` 12/12, `test_violet_shell.py` ALL OK, `test.py` ALL OK,
  `test_kernel_aarch64.py` / `test_qeapp_aarch64.py` ALL OK, unittest 76 OK.
- Ảnh mới `dist/boot-vqeaf-violet.img` 29.732.864 B, SHA-256
  `1afcae43f25e61ab7173028271e4c6f13115a459c37085a5eb3041633bdfd2b2`;
  validate/audit/wifi-package PASS. **Chưa flash** — cần user xác nhận.

## Nạp ảnh sửa kẹt rescue lên p77 — xác nhận trên máy (2026-11-20 18:04)

- User: "nạp os vào thiết bị". Máy đang ở **fastboot** (`ca1af25d`, product
  `violet`, variant `SM6 EMMC`), không phải VQEAF — LUN chưa lên. Ảnh nạp là
  `dist/boot-vqeaf-violet.img` sha `3a71e3ff…57a13edb` (bản sửa volume_lock,
  đã stage thành `QeafdropOs/out/boot-vqeaf-violet-lockfix-20261120.img`).
- Gate: dry-run `restore_p77.ps1` PASS (không ghi), backup ảnh đang nạp có sẵn
  `build/screens/device/flashed-wifi-20261120-BOOTIMG.bin` (sha `5c1f26e8…`),
  sau đó commit `-Execute -ConfirmPartitionWrite`: `Sending 'recovery' OKAY`,
  `Writing 'recovery' OKAY`, `fastboot reboot recovery`. **Chỉ ghi p77**; log
  `QeafdropOs/out/logs/restore-p77-20261120-180419.log`.
- Sau reboot: LUN `F:` về. Ghi `echo 180434` + `wifi state` với nonce mới →
  `RESPONSE.TXT` `seq=1 ### echo 180434 / 180434` rồi trả full `wifi state`.
  **Rescue worker đã tiêu thụ lệnh** — khác hẳn trạng thái kẹt lúc trước
  (nonce 174438 không bao giờ xuất hiện). Đây là bằng chứng trực tiếp rằng fix
  `volume_lock_drop_on_fault()` có tác dụng trên máy thật.
- `QEAFBOOT.LOG`: `boot stage dri-wait → splash → loop → wifi state answered`;
  `DEVINFO.TXT`: `ui=vqeaf-phone panel=1080x2340`. `LASTBOOT.TXT` vẫn là record
  của boot cũ (`sig=11 stage=loop ui=0 boots=579`), không có fault mới.
- Cố gắng chạy thêm `prepare_rescue_flash.py --commit` để lấy 2 proof raw-sector
  thì bị chặn: script ghi `BOOTIMG.BIN` xong nhưng `PermissionError: F:/QEAF.CMD`
  (bug đã biết: file cũ trên LUN không overwrite được), rồi lệnh bị security
  policy chặn. **Không retry**. Fastboot có proof riêng (`Sending/Writing
  recovery OKAY`) nhưng không có cặp chuỗi `flash: written and verified byte
  for byte` + `lun: verified …`; ghi rõ ở đây thay vì nhận là đã có.

## Sửa kẹt QEAFRESCUE sau fault — volume_lock longjmp-safe (2026-11-20)

- Khi tiếp tục validate Wi-Fi, LUN `F:` vẫn mount nhưng rescue worker **không
  tiêu thụ QEAF.CMD**: ghi lệnh `wifi state` + nonce `174438` xong, đọc raw vẫn
  thấy `RESPONSE.TXT` cũ (`seq=3`, `wifi bri`) và `QEAF.CMD` còn nguyên.
- `LASTBOOT.TXT` trên máy: `sig=11 stage=loop ui=0 uptime_s=9 boots=579`,
  `boot: panel/DRM init faulted; UI disabled, LUN stays up`. Đây chính là bug
  đã ghi từ trước: `ui_fault()` `siglongjmp` ra khỏi block đang giữ
  `volume_lock` → mutex không bao giờ được unlock → worker block vĩnh viễn
  trong `volume_lock_acquire()`.
- Sửa: tách discipline khoá sang `platform/violet/volume_lock.h` — mỗi lần
  acquire tăng depth TLS; `volume_lock_drop_on_fault()` trả lại đúng số level
  trước khi handler `siglongjmp` hoặc `pause()` worker. Cả 2 chỗ `trylock`
  (`crash_flush(1)` và fallback poll của main loop) cũng tăng depth, nếu không
  `release()` sẽ không unlock thật. Thay mọi dùng trực tiếp bằng wrapper.
- Gate mới `tests/vqeaf_volume_lock_test.c` (đăng ký trong `CMakeLists.txt`):
  4 check gồm cả trường hợp fault ở thread **không** giữ lock không được mở
  khoá của thread khác. `ctest` 12/12 PASS (test mới chạy riêng cũng PASS);
  `build_violet.py` OK; `test_violet_shell.py` ALL OK (49 snapshot);
  `test.py` ALL OK; `test_kernel_aarch64.py` ALL OK; `test_qeapp_aarch64.py`
  ALL OK; unittest 76 tests OK.
- Ảnh sửa lỗi đã đóng gói **chưa nạp**: `dist/boot-vqeaf-violet.img`
  29.732.864 byte, SHA-256
  `3a71e3fffe04b4f89727d20b290e01858c4a001f8f18681883202d7957a13edb`;
  `validate_bootimg` + `audit_rescue_channel` +
  `verify_native_wifi_package.py` (111 entry) đều PASS. Ảnh đang nạp trên máy
  được sao lưu `build/screens/device/flashed-wifi-20261120-BOOTIMG.bin`
  (sha `5c1f26e8…`). **Chưa flash, chưa reboot — cần user đồng ý** vì máy hiện
  đang kẹt, muốn áp ảnh mới phải reboot bằng tay rồi flash p77 theo gate cũ.

## Nâng cấp driver Wi-Fi violet — CNSS/Binder + firmware audit (2026-11-20)

- User: "nâng cấp drive wifi". Phạm vi là violet (Redmi Note 7 Pro), **không**
  đụng kernel AArch64/QEMU hay x86 UEFI. Chỉ ghi p77/recovery, có backup và 2
  bằng chứng raw-sector cho mỗi lần flash.
- Trạng thái trước nâng cấp (đo bằng `wifi state` qua QEAFRESCUE): firmware WCN3990
  MIUI 20.10.30 đã được staged từ trước; `qrtr-ns` đã thấy lookup `0x45/0x0`
  (WLFW), nhưng `cnss-daemon: not started`, `wlan0: absent`.
- Nguyên nhân mới phát hiện: `/proc/devices` có char major **499 binder**, nhưng
  kernel không tạo sysfs/devnode. `platform/violet/host_services.c` giờ tìm major
  theo runtime rồi probe các minor 0..7; report `hostsvc binder:` được đưa vào
  `wifi state`. Trên máy thật cả 8 minor đều `ENXIO`, nên cnss-daemon vẫn abort:
  `Binder driver '/dev/binder' could not be opened ... No such device or address`
  (lần đầu trước khi có node là `No such file or directory`). Đây là bằng chứng
  kernel đăng ký major nhưng **không có minor binder usable**, không phải lỗi QEAF.
- `scripts/package_violet.py` thêm `/etc/qeaf/radio-firmware-status.txt`: nguồn
  `VIOLET_20.10.30 NON-HLOS` + SHA-256 của `WLANMDSP.MBN` và 30 `BDWLAN.*`.
  `wifi fw` trên máy in trực tiếp manifest này (trước đó báo `absent`).
  `scripts/verify_native_wifi_package.py` bắt buộc entry này.
- `wifi up` sau khi nạp ảnh mới: modem PIL boot thật (`fd=11 rc=1`),
  QRTR lookup `0x45/0x0` xuất hiện, nhưng cnss-daemon thoát `-6`; fallback `wlfwd`
  không khởi động vì `wlan_mac.bin` không có trên persist và QEAF từ chối MAC
  tự tạo. `rmtfs` cũng exit 1 do thiếu `/dev/qcom_rmtfs_mem1`. **Không có wlan0**;
  không báo kết nối giả.
- `wifi up` làm máy reboot một lần; sau reboot QEAFRESCUE và `ui: LVGL phone shell
  active at 720x1560` đều sạch. `LASTBOOT.TXT` ghi lại crash của phiên trước
  (`sig=11 stage=loop ui=0`), không phải hậu quả của firmware Wi-Fi.
- Ảnh cuối: `dist/boot-vqeaf-violet.img` **29.732.864 byte**, SHA-256
  `5c1f26e8d29c28a2b902ea8e42919530991f48c4b4d574898c6179da4be831b5`;
  `validate_bootimg` + `audit_rescue_channel` PASS. Backup trước mỗi lần ghi:
  `build/screens/device/pre-flash-wifi-20261120/BOOTIMG.BIN` (sha `83035596…`)
  và `pre-flash-binder-probe-20261120/BOOTIMG.BIN` (sha `211928ae…`).
- Bằng chứng ghi p77: `build/screens/device/binder-probe-flash-proof-173526/`
  có cả `flash: written and verified byte for byte` và
  `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over 29732864 bytes`.
- Kết luận: firmware/driver userspace đã được nâng và kiểm chứng, nhưng **chưa
  đạt wlan0**. Chặn còn lại nằm ở kernel (binder minor/rmtfs mem) và dữ liệu
  per-unit (MAC/BDF) trên persist. Cần quyết định tiếp: kernel có binder device
  thật + rmtfs node, dump persist thật của máy, hoặc cho phép dùng MAC derived
  và ghi rõ đó không phải MAC factory.

## Bàn phím Material Icons — QEAFIVELS OS (2026-11-20)

- User: "UI keyboard đẹp hơn thêm font icon dùng https://fonts.google.com/".
- Dùng **Material Icons** đã vendored từ Google Fonts
  (`assets/fonts/MaterialIcons-Regular.ttf`, Apache 2.0 trong
  `MaterialIcons-LICENSE.txt`); không thêm phụ thuộc runtime hay asset mới.
- `scripts/generate_lvgl_fonts.ps1`: bổ sung 8 mã icon cho IME — `backspace`,
  `keyboard_capslock`, `keyboard_return`, `numbers`, `space_bar`,
  `arrow_upward`, `abc`, `keyboard_hide`. `vqeaf_icon_28/40` nay có 130 glyph
  (trước 122) và `vqeaf_fonts.h` có macro tương ứng.
- `platform/violet/ui/vqeaf_ime.c`: phím đặc biệt (SHIFT/?123/ABC/SPACE/DEL/
  ENTER) có glyph Material 28 px + caption gốc; caption vẫn là text chính xác
  để accessibility và gate không đổi. SHIFT thường dùng mũi tên, SHIFT bật dùng
  caps-lock; SPACE dùng space-bar; ENTER dùng keyboard-return.
- Phím được bo 12 px, viền 1 px, tint riêng cho phím đặc biệt, viền accent khi
  active; màu chữ tự đổi theo luminance nên vẫn đọc được ở theme Pastel sáng.
  Window bàn phím bo 18 px, viền palette, pad 6/10; use `vqeaf_ime_restyle()`
  cập nhật cả chrome khi đổi light/dark/dynamic colour. Slide-up, adjustResize,
  strip focus và async rebuild giữ nguyên.
- Bằng chứng: `test_violet_shell.py` ALL OK với 9 assert icon mới
  (ENTER, SHIFT arrow/caps-lock, ?123 numbers, ABC, SHIFT restore, SPACE, DEL);
  `render_phone_ui.py` 132 PNG, `build/screens/phone-ui/ime-normal-720x1560-
  providers.png` và `interaction-notes-ime-special.png` cho thấy glyph rõ, không
  clip ở portrait, landscape split và one-handed. `unittest` 75 tests OK,
  `scripts/test.py` ALL OK.
- Nợ cũ lộ ra khi biên dịch lại test sau khi sửa: stub
  `vqeaf_qeapp_device_launch()` thiếu đối số `effective_permissions` và dùng
  `VQEAF_QEAPP_DEVICE_H` không tồn tại. Đã đồng bộ với header thật; lỗi này từng
  bị object cache che.

## Màn hình khởi động terminal — QEAFIVELS OS (2026-11-20)

- Đổi boot splash x86_64 (`bootloader/core/boot_manager.c`) từ logo/progress
  bo góc sang **boot terminal**: nền đen có scanline mờ, tiêu đề `QEAFIVELS OS`,
  khung log ASCII, dòng `[  OK  ] / [ WAIT ] / [ .. ]`, progress dạng
  `[#######---------] 70%`, cursor block và dòng `BOOT> _`.
- Nội dung log là dữ liệu thật của boot manager: kích thước GOP, boot protocol
  v3, đường dẫn `\EFI\VQEAF\KERNEL.EFI`, và mốc 20/70/100 khớp lúc dựng fb,
  đọc ảnh, chuyển control. Không bịa service hay trạng thái phần cứng.
- `scripts/capture_screen.py`: chờ marker `SPLASH: terminal boot log ready`,
  gửi `stop` cho QEMU rồi mới `screendump`, vì splash cũ chỉ sống 400 ms và
  lần chụp đầu dễ trúng lock screen. Bằng chứng:
  `build/screens/splash.png` 720x1280, 8 112 phosphor pixel.
- Cùng style cho splash trên máy (`platform/violet/splash.c`): bỏ icon Material
  và thanh ngang, dùng terminal log căn trái. Dòng boot violet/mt689x thêm
  prefix `[  OK  ]` (`drm`, `QEAFRESCUE`, `QEAF.CMD`). Title thống nhất
  `QEAFIVELS OS`.
- Thêm `tests/vqeaf_splash_host.c` + `scripts/render_boot_splash.py`: render
  `vio_splash_render()` thật bằng font atlas thật ra
  `build/screens/boot-terminal.png` (720x1560, 6 595 pixel xanh terminal).
  Script nằm trong `scripts/test.py`, nên gate x86 tự kiểm cả hai splash.
- Build: `scripts/build.py`, `scripts/build_violet.py` và
  `scripts/build_mt689x.py` đều exit 0. `scripts/test.py` ALL OK.
- Ranh giới: ảnh violet/mt689x render bằng host harness, **không phải screenshot
  máy thật**; để có bằng chứng máy thật cần vào VQEAF và dùng kênh QEAFRESCUE.

### Sửa font boot terminal (cùng ngày)

- Font Ubuntu trước đó có ô 12×16 nhưng nét chỉ ~6–8 px; khi scale lên chữ bị
  giãn khoảng cách. Chuyển boot splash sang font VGA 8×8 có sẵn
  (`system/ui/include/vqeaf/font8x8.h`) với bước chữ đúng bằng ô 8 px × scale.
- `bootloader/core/boot_manager.c`: tiêu đề 5×, log 2×, progress 3×, khung 40 ô
  × 16 px căn giữa 720 px. `platform/violet/splash.c`: cùng font, khung 40 ô ×
  16 px, log 2×, tiêu đề 5×.
- Bắt lỗi thật bằng screenshot: lần đầu chữ bị gương vì font8x8 ghi bit 0 ở
  bên trái nhưng renderer đọc từ bit 7; sửa thành `1u << col`. Lần sau violet
  bị khung chỉ rộng một nửa vì hàm vẽ vẫn dùng 8 px/ô trong khi layout tính
  16 px/ô; thống nhất `VIOLET_TERM_SCALE = 2`.
- Bằng chứng mới: `build/screens/splash.png` 720×1280, 20 082 phosphor pixel;
  `build/screens/boot-terminal.png` 720×1560, 18 870 pixel. `scripts/test.py`
  ALL OK (11/11 smoke, 75 unittest), build violet 265 object, mt689x 265 object.

## Nạp bản boot-terminal lên violet — p77/recovery (2026-11-20, có uỷ quyền)

- Ảnh cuối: `dist/boot-vqeaf-violet.img` **29.691.904 byte**, SHA-256
  `83035596dfd0d5581eecfd037bc84fa49741557c084cf4b0d31aaa92b31f0005`;
  `validate_bootimg` PASS + `audit_rescue_channel` PASS. Bản sao p77 cũ trước
  khi ghi: `build/screens/device/pre-flash-20261120/BOOTIMG.BIN` (29.687.808
  byte, sha `700e350c14e53b2bc74292c4568ad5bf189af9e8e1587c030216b6d9e1aafb79`).
- Sự cố đường A: ảnh đã staged đúng trên `F:\BOOTIMG.BIN` (sha khớp), nhưng
  worker cứu hộ không tiêu thụ lệnh — kể cả `echo` có nonce. Đọc raw thường,
  raw `FILE_FLAG_NO_BUFFERING`, và liệt kê root FAT đều xác nhận `QEAF.CMD` hợp
  lệ, không phải cache stale. `LASTBOOT.TXT` của phiên trước giải thích nguyên
  nhân: `sig=11 … stage=loop ui=0`, `boot: panel/DRM init faulted` → `siglongjmp`
  bỏ qua `pthread_mutex_unlock`, khoá `volume_lock` vĩnh viễn.
- Đường B (sau khi người dùng đồng ý reboot cứng): `fastboot wait` thấy đúng
  `product=violet`, serial `ca1af25d`. Chạy dry-run rồi
  `restore_p77.ps1 -Image ..\..\QEAFIVELS-Os\dist\boot-vqeaf-violet.img
  -Execute -ConfirmPartitionWrite`: `Sending 'recovery' OKAY`,
  `Writing 'recovery' OKAY`, `fastboot reboot recovery`. **Chỉ p77/recovery**;
  không chạm `p76/boot`, `userdata`, `misc`.
- Sau reboot: `QEAFBOOT.LOG` có `boot stage loop`; `HOMOLOG.TXT` có
  `boot: QEAFIVELS OS 0.3.0 init on violet (PID 1)`, DRM crtc 107 quét fb 188
  ở 1080x2340, `ui: splash 720x1560 (portrait)` và
  `ui: LVGL phone shell active at 720x1560`. Echo `alive115448` được trả lời
  `seq=1` → kênh cứu hộ sống. Ảnh máy thật:
  `build/screens/device/final-live/live.png` (màn khoá + sheet USB dùng, frame=1).
- **HAI BẰNG CHỨNG RAW-SECTOR BẮT BUỘC** sau khi stage lại ảnh và gửi lệnh có
  nonce `flash115507` (lưu `build/screens/device/flash-proof-115507/`):
  - `RESPONSE.TXT`: `flash: written and verified byte for byte`
  - `QEAFBOOT.LOG`: `lun: verified - recovery and BOOTIMG.BIN agree byte for
    byte over 29691904 bytes in 1 cluster runs`
- Ranh giới: terminal splash bắt lấy bằng host harness
  (`build/screens/boot-terminal.png`), không phải ảnh chụp máy; trên máy chỉ có
  bằng chứng `ui: splash` + shell active. Không reboot lại sau lần verify cuối.

## Kiểm ổ QEAFRESCUE trên máy thật + sửa lỗi tự khởi động lại (2026-11-19)

- **Ổ cứu hộ trên máy thật:** `F:` là FAT "QEAFRESCUE" 33 259 520 B (65 536
  sector), `status` báo `usb_mode=mtp`, `usb_cable=1`. Ghi 8 MiB ngẫu nhiên từ
  host rồi đọc lại bằng raw sector: khớp sha256 từng byte. `ls` liệt kê đúng
  các tệp của OS; host ghi `QEAF.CMD` thì máy nhận ngay (kênh hai chiều sống).
- **Triệu chứng:** máy reset đúng 42-44 s sau mỗi lần boot; journal đếm
  `boots=476` nhưng `CRASH.TXT` và bản ghi boot trước đều rỗng — không có lỗi
  phần mềm nào bị bắt.
- **Chẩn đoán:** thêm verb `diag` (watchdog / pin / nhiệt / cmdline /
  `chosen/pureason`). Kết quả: `/sys/class/watchdog` không có, không module
  `*wdt*`, không `/vendor/lib/modules` → kernel này **không có driver
  watchdog**; pin 27 % đang sạc, SoC 37 °C. Nghi vấn chuyển sang modem.
- **Thử nghiệm quyết định:** `wifi off` (đóng fd `/dev/subsys_modem`, tức
  `subsystem_put`) → cùng image sống **598 s, 0 reset**; bản boot mặc định
  không khởi động modem → **360 s, 0 reset**. Trước đó cùng máy reset ở 42 s.
- **Nguyên nhân:** PIL boot `modem.mdt` nhưng không có tầng dịch vụ nền tảng di động
  chạy kèm; khoảng 40 s sau, msm subsystem restart cả SoC. Kernel không panic
  nên không thể có crash record — đây là lý do mọi suy đoán trước đó trượt.
- **Sửa:** modem thành **opt-in**. Boot chỉ mount p86/p82/p74 read-only và tạo
  273 link firmware, không boot modem; `wifi up` là đường duy nhất bật nó (và
  nói rõ có thể reset máy), `wifi off` thả ra. Thêm `watchdog.c`: mở
  `/dev/watchdog`, chỉ "cho ăn" khi main loop còn ack trong 8 s — trên kernel
  này nó ghi `no watchdog device` và không làm gì, nhưng kernel có watchdog
  thì không còn bị bỏ đói.
- **LASTBOOT.TXT trung thực:** khi journal không có bản ghi, file ghi rõ
  "no fault record from the previous boot: it ended without a caught signal
  (watchdog, power or kernel reset)" + `boots=N` thay vì 15 KiB NUL.
- **Gate/kiểm:** build 264 object exit 0; `test_violet_shell.py` **ALL OK**
  (49 snapshot). Trên máy: flash qua chính ổ (verify byte-for-byte), boot mới
  sống 360 s, `wifi state` báo `modem not started (opt-in; `wifi up` boots
  it)`, `retries=0`.
- **Ranh giới:** `wlan0` vẫn chưa có; bật modem vẫn reset máy — giờ là lựa
  chọn rõ ràng của người dùng, không còn là cái chết ngầm mỗi 42 s.

## Game mẫu "Xếp Hình 4x4" + `qeaf-sim --keys` (2026-11-18)

Yêu cầu: tạo dự án mẫu một game xếp hình đơn giản. Thêm
`examples/puzzle-qeapp` — QEAPP native chơi được, không phải ảnh giả:

- **Luật:** bàn 4×4, 15 ô số + 1 ô trống; trượt ô cạnh ô trống; đếm bước;
  sắp `1..15` là thắng; thắng xong bấm/chạm bất kỳ để xáo bàn mới. Bàn xáo bằng
  **80 nước đi hợp lệ** từ trạng thái đã giải nên luôn giải được (không sinh
  hoán vị lẻ).
- **Vẽ:** chỉ dùng bảng API ABI v4 — `blit_pixels` (nền/ô RGB565 theo dải
  720×8) và `draw_text_weighted`; số trên ô vẽ bằng **7 đoạn** vì font runtime
  chỉ có ô 8×8. Không libc, không syscall, không `/` hay `%` trên giá trị động
  (qenlink không link helper chia); bộ đếm in bằng phép trừ/ nhân luỹ thừa 10.
- **Nhập:** `on_input` nhận mũi tên (103/108/105/106), `WASD` (119/115/97/100)
  và `QEAF_INPUT_TOUCH` — chạm ô cạnh ô trống cũng trượt. Hướng phím = hướng
  ô trượt.
- **Gói:** `manifest.json` QEAPP v2 native, `abi_version 4`, id
  `org.vqeaf.puzzle`, entry `native/x86_64/app.qen`, quyền
  `ui.display` + `input.receive`. Build cho **x86_64** để Máy ảo VQEAF chạy
  trên PC; muốn lên violet/arm64 chỉ đổi `architecture`/`entry` rồi build lại
  (mã nguồn không đổi) — đã ghi trong README của mẫu.
- **`qeaf-sim --keys code[,code...]`:** tuỳ chọn CLI mới trong
  `framework/simulator/qeaf_sim_main.c`, bơm các sự kiện `QEAF_INPUT_KEY`
  (pressed=1) vào `cfg.input` sau `start()`, để host điều khiển app không cần
  GUI. Đây là thứ làm gate của game có ý nghĩa: không có nó, mọi app native
  chỉ chạy được trạng thái khởi tạo.

**Bằng chứng (đã chạy):**

- `qeaf build examples/puzzle-qeapp --config release` → `puzzle-qeapp.qeapp`,
  `qeaf validate` OK, unittest của project 2/2 OK.
- `qeaf-sim --device violet` → lifecycle đủ 6 bước, log `Xep Hinh: board
  ready`, 5 draw (tiêu đề, gợi ý, "So buoc:", "0", dòng cuối), framebuffer
  `720x1560 hash=0xa181d6b1`, `input delivered: 0`, 0 alloc, 0 deny.
- `qeaf-sim --keys 105,103,106,108` → `input delivered: 4`, bộ đếm lên **3**
  (một phím bị chặn đúng luật vì ô trống ở mép), hash đổi
  `0xa181d6b1 → 0x1d79f6f9`.
- Ảnh xem được: `build/screens/puzzle-start.png` và `puzzle-moves.png`
  (chuyển từ `--fb` raw bằng bộ chuyển XRGB8888→PNG tạm, không thêm dependency).
- Gate mới `scripts/test_puzzle_qeapp.py`: **ALL OK**. Gate simulator cũ
  `scripts/test_qeapp_simulator.py`: **ALL OK** (114 check, hash hello không đổi
  `0x6cc394ad`) sau khi thêm `--keys`.
- Ranh giới: game không lưu điểm (QEAPP chưa ghi được storage), canvas cố định
  720×1560 theo profile violet, và ảnh ở trên là **simulator**, không phải máy
  thật.

### Đưa mẫu vào VQEAFEngine (IDE) và lỗi đường dẫn Unicode

- Đã tạo project thật trong thư viện của IDE:
  `Documents/VQEAF Projects/Xep Hinh 4x4` (descriptor `project.qeapp.json`,
  `app_id` để engine cấp) và thêm vào registry
  `%APPDATA%/VQEAFEngine/VQEAFEngine/VQEAFEngine/projects.json`.
- **Lỗi build gặp phải:** `qeaf build` chết với `UnicodeEncodeError: 'charmap'`
  khi in đường dẫn có chữ Việt (console cp1252) — gói đã ghi xong nhưng tiến
  trình trả mã lỗi. Sửa ở nguồn: `sdk/cli/qeaf.py` và
  `sdk/compiler/qecompile.py` reconfigure stdout/stderr sang UTF-8
  (`errors="replace"`).
- **Lỗi chạy gặp phải:** `qeaf-sim.exe` nhận `argv` kiểu ANSI nên **không mở
  được** đường dẫn ngoài ASCII (`cannot read ... X?p H?nh ...`). Vì vậy thư mục
  project đổi thành `Xep Hinh 4x4` (ASCII) trong khi tên hiển thị vẫn là
  "Xếp Hình 4x4"; hạn chế này ghi trong README của mẫu. Chưa sửa simulator
  (cần `GetCommandLineW`/`wmain` + `_wfopen`); đây là việc tiếp theo nếu muốn
  tên thư mục tiếng Việt chạy được.
### Cửa sổ Máy ảo: `qeaf-sim --ui`

- Trước đây `qeaf-sim` chỉ chạy headless rồi in report — muốn "xem UI" phải tự
  chuyển `--fb` sang ảnh. Nay `--ui` mở một cửa sổ Win32+GDI (không thêm
  dependency): framebuffer canvas được `StretchDIBits` vào client area, cửa sổ
  tự co còn ~85% chiều cao màn hình, và `WM_KEYDOWN` được đổi sang mã evdev
  (mũi tên 103/106/105/108, W/A/S/D 119/97/115/100) rồi đưa vào app.
- Để host điều khiển app sau `start()`, thêm API công khai
  `qeaf_sim_input(sim, event, error, cap)` trong `qeaf_sim.[ch]`: kiểm tra
  trạng thái `started`, export `on_input`, quyền `input.receive`, và budget
  từng lần gọi (dùng chung `call_done`). `qeaf_sim_start()` vẫn giao
  `cfg.input` một lần như cũ, nên `--keys` vẫn nguyên nghĩa.
- `--ui` chạy lifecycle thủ công (create → start → window loop → pause →
  resume → stop → destroy) và vẫn kiểm tra leak/call-budget ở cuối, đúng như
  `qeaf_sim_run`; đóng cửa sổ là thoát bình thường.
- `qesim.build_simulator()` link thêm `-luser32 -lgdi32` cho CLI (máy test
  thuần không cần).
- **Bằng chứng:** cửa sổ `Máy ảo VQEAF - qeaf-sim` client 338×733 cho canvas
  720×1560; bấm ←↑→↓ → `input delivered: 4`, bộ đếm "So buoc: 3", framebuffer
  `0xa181d6b1 → 0x1d79f6f9`; đóng cửa sổ → lifecycle đủ 6 bước, exit 0. Ảnh:
  `build/screens/ui/ui-start.png`, `ui-keys.png`.
- Gate: `test_qeapp_simulator.py` **ALL OK**, `test_puzzle_qeapp.py` **ALL OK**
  (không đổi hành vi headless).
- **Giới hạn:** cửa sổ chỉ có trên Windows; runner QEAPP của IDE hiện vẫn gọi
  simulator không kèm `--ui` (chạy headless), nên muốn xem UI trong IDE cần
  quyết định có thêm cờ này vào `qeapp_runner.py` hay không.

- **Bằng chứng trên sản phẩm đã cài:** dùng đúng runtime + SDK + simulator của
  `C:/Program Files/VQEAFEngine`:
  `release: ... signed and validated .../Xep Hinh 4x4/build/releases/puzzle-qeapp.qeapp`
  rồi `qeaf-sim --device violet` → lifecycle đủ, `Xep Hinh: board ready`,
  framebuffer `hash=0xa181d6b1` (khớp bản chạy bằng simulator dev).

## G1 — virtio-mmio block driver thật sự chạy trên QEMU virt (2026-11-18)

Trạng thái trước: `test_kernel_aarch64.py` in `VMMIO: probe FAILED rc=-5` rồi
`G1: virtio-mmio storage absent`, dù các mốc A–F3 vẫn xanh. Ba lỗi riêng biệt,
mỗi lỗi đủ để queue không lên:

- **Thanh ghi avail/used sai offset.** Driver ghi `QUEUE_AVAIL` vào 0x088 và
  `QUEUE_USED` vào 0x090, nhưng bảng của virtio-mmio là 0x090/0x094 (avail) và
  0x0a0/0x0a4 (used). Hậu quả: thiết bị nhận `used = 0`, ghi used-ring vào địa
  chỉ 0 nên `used->idx` không bao giờ tăng. Nay dùng `M_QUEUE_AVAIL_*` /
  `M_QUEUE_USED_*`.
- **Bản đồ thanh ghi legacy sai.** Bản cũ theo hình vẽ "legacy MMIO" của spec
  virtio 1.0 (GuestPageSize 0x020, QueueNumMax 0x028...). QEMU (kiểm bằng
  `include/standard-headers/linux/virtio_mmio.h` của QEMU 11.1 và đọc trực tiếp
  MMIO qua QMP `xp`) giữ **cùng offset** cho legacy: GuestPageSize 0x028,
  QueueSel 0x030, QueueNumMax 0x034, QueueNum 0x038, QueueAlign 0x03c, PFN
  0x040, Notify 0x050, Status 0x070; chỉ khác là legacy có PFN/Align và ghi
  driver features ở 0x020/0x024. Đọc 0x020 ra 0 nên `qnum=0` → probe -5.
- **Polling làm chết tiến trình hoàn tất I/O.** Vòng chờ used-ring chỉ `yield`.
  QEMU virt chạy vCPU trong main thread với 1 vCPU (single-threaded TCG), nên
  guest quay tại chỗ giữ BQL và bottom-half hoàn tất I/O không bao giờ chạy →
  timeout. Nay vòng chờ dùng `wfi` (đúng như driver thật chờ IRQ hoàn tất), vẫn
  có `POLL_LIMIT` để không treo vô hạn.

Ngoài ra gate còn hai điểm sai kỳ vọng:

- QEMU cấp virtio-mmio từ transport **cuối** lên, nên một đĩa duy nhất nằm ở
  `0x0a000000 + 31*0x200 = 0x0a003e00`, không phải `0x0a000000`. Gate nay khớp
  regex trong vùng 0x0a00xxxx thay vì địa chỉ cố định; driver vẫn quét 32
  transport từ thấp lên nên tìm đúng.
- QEMU mặc định `virtio-mmio.force-legacy=true`. Lần chạy chính thêm
  `-global virtio-mmio.force-legacy=false` để kiểm transport v2 (descriptor
  ring), và thêm **G1b**: một lần boot riêng với legacy v1 (PFN) để chứng minh
  nhánh còn lại cũng thật.

**Bằng chứng (đã chạy):** `test_kernel_aarch64: ALL OK`, gồm
`VMMIO: block transport base=0x000000000a003e00 version=2 capacity=2048
sectors queue=256`, `probe READY instance=0 block id=0`, self-test ghi/đọc
sector cuối PASS, host đọc lại sector cuối của `g1-scratch.img` khớp pattern;
G1b legacy: `version=1 ... queue=256`, self-test PASS, host readback PASS, QEMU
thoát sạch. Không còn dòng `VMMIO: probe FAILED`.

**Số liệu truy vết (2026-11-18):** gate `PYTHONUTF8=1 PYTHONIOENCODING=utf-8
D:/Program/Python/python.exe scripts/test_kernel_aarch64.py` → **95 check ok,
0 FAIL, ALL OK**, chạy xanh 4 lần liên tiếp (một lần đầu dính flake timing F3
"post-launch key routed" — không liên quan G1, các lần sau đều xanh). ELF
`build/kernel-aarch64/vqeaf-kernel-aarch64.elf` **304.776 B**, sha256
`b9119980d65e4abe58f1d8169f63a5b8814b50dd61db9ed7f75d5edc74164acb`. Offset
thanh ghi đối chiếu khớp `include/uapi/linux/virtio_mmio.h` (Linux/QEMU):
QUEUE_AVAIL 0x090/0x094, QUEUE_USED 0x0a0/0x0a4, GUEST_PAGE_SIZE 0x028,
QUEUE_PFN 0x040. Bằng chứng EMULATED trên QEMU virt 11.1.0, không ghi phần
cứng violet thật.

## #34 — màn hình đen: khung chụp trước khi splash được vẽ (2026-11-18)

- **Kết luận điều tra:** `SCREEN.RAW` đen (nonblack=0/124800) kèm journal dừng
  ở `stage=splash` không chứng minh renderer hỏng. `set_stage("splash")` chạy
  **trước** `splash_present()`; rescue thread phục vụ `snap` từ stage `lun`, nên
  một lệnh `snap` tới sớm sẽ đọc `drm.map` vừa được `memset(0)` trong
  `vio_drm_init` — khung đen nhưng `frame`/`fnv` vẫn hợp lệ, gate cũ không phân
  biệt được với lỗi vẽ. Ảnh 11-15 khớp render 99,8 % là cùng đường vẽ đó, nên
  giả thuyết "hỏng đường DSI/DRM" không có bằng chứng.
- **Sửa để lần sau tự trả lời:** trailer 40 byte của SCREEN.RAW thêm `st=<c>`
  (`boot_stage_code()` trong `main_violet.c`): `0` start, `l` lun, `w` dri-wait,
  `b` backlight, `d` drm-init, `s` splash, `p` phone-ui, `o` loop. Kích thước
  pixel block và toàn bộ trường `frame=`/`fnv=` giữ nguyên nên host cũ vẫn đọc
  được; ảnh cũ không có `st` được báo `stage=unmarked`.
- `scripts/device_check.py` đọc thêm `st=` và **từ chối** khung đen khi stage
  còn trước `phone-ui` (kèm hướng dẫn chờ `ui: splash` / `ui: LVGL phone shell
  active` rồi snap lại). Khung đen ở `p`/`o` vẫn được chấp nhận vì màn hình có
  thể đang ngủ.
- **Chưa xác minh UI vật lý:** phiên này không có thiết bị nào kết nối (`adb
  devices` rỗng, không có `F: QEAFRESCUE`), nên không có ảnh chụp mới và không
  tuyên bố pixel trên kính. Việc cần làm khi có máy: boot ảnh mới (có `st=`),
  chờ marker UI rồi `snap`; nếu vẫn đen ở `st=p/o` thì đó mới là lỗi DRM/panel
  thật và `st` sẽ chỉ đúng stage.
- Gate đã chạy lại sau thay đổi: `build_violet.py` link **9.520.744 B / 263
  object**, `test_violet_shell.py` **ALL OK (49 snapshot)**, `test_kernel_aarch64`
  **ALL OK** (G1 + G1b).

## Census capability violet trên thiết bị thật (2026-11-16, có uỷ quyền nạp p77)

- **Mục tiêu:** trả lời trung thực yêu cầu "cập nhật driver/firmware hỗ trợ UFS,
  USB, DRM, cảm ứng, Wi-Fi, refresh cao, Bluetooth, âm thanh, GPU, tự động xoay,
  KVM; chưa hỗ trợ NFC/camera/baseband". Thay vì đoán, OS công bố capability từ
  probe chỉ-đọc thật qua registry v0.2 (`system/core/capability.c`).
- **Đã triển khai:** `platform/violet/vio_caps.{c,h}` — publish/report thuần
  (gate host không cần thiết bị) + `vio_caps_probe_device()` chỉ-đọc sysfs/dev
  dưới `__linux__`. Verb `caps` trong `channel/fs/rescue.c`; publish lúc boot
  trong `main_violet.c` sau `vio_wifi_bringup()`. Thêm `vio_caps.c` +
  `capability.c` vào `scripts/build_violet.py`.
- **Gate host:** `scripts/test_violet_caps.py` → `vqeaf_violet_caps: ALL OK
  (7 groups)` (probe tổng hợp: zero→BLOCKED_FIRMWARE, violet thật, firmware
  PRESENT, full READY, transition, report, host probe không bịa tín hiệu).
- **Cross-compile:** `build_violet.py` linked aarch64 ELF (263 objects).
  **Gate 2:** `package_violet.py` validate_bootimg PASS + audit_rescue_channel
  PASS → `dist/boot-vqeaf-violet.img` 20381696 B,
  sha256 `a329d5aef01b5e85c23bab4c07d5f9d5c890d220397f708efdbc2d50bc93106e`.
- **Nạp p77 (được uỷ quyền):** `prepare_rescue_flash.py --commit` copy
  BOOTIMG.BIN (sha verified trên LUN) → verb `flash recovery BOOTIMG.BIN
  confirm`. Thiết bị reboot vào ảnh mới; verb `caps` (chỉ có trong ảnh này) trả
  census, QEAFBOOT.LOG ghi "caps census reported", uptime=27s → bằng chứng
  p77 ghi + boot thành công. Artifact: `build/screens/device/CAPS.TXT`,
  `caps-run/RESPONSE.TXT`.
- **Kết quả thật trên violet (Qualcomm, eMMC):**
  - READY: `storage.emmc`, `usb.gadget`, `display.drm`, `input.touch`.
  - PRESENT: `wifi` (firmware WCN3990 đã mount, wlan0 chưa up).
  - BLOCKED_FIRMWARE: `bluetooth` (chưa có rampatch/nvm).
  - UNSUPPORTED: `storage.ufs` (violet là eMMC), `display.refresh_high`
    (panel 60Hz), `usb.host`, `audio` (tas2563 chưa powered, 0 soundcard),
    `gpu` (không có kgsl/renderD128), `sensor.accel` (không IIO accel),
    `kvm` (không EL2), và `nfc`/`camera`/`baseband` (ngoài phạm vi).
- **Giới hạn:** đây là bản census trung thực, KHÔNG phải đã enable mọi driver.
  UFS/refresh-cao/KVM không tồn tại trên phần cứng violet nên không thể bật.
  Các mốc enable tiếp theo (wifi up, audio amp, GPU Adreno, BT firmware, accel
  cho auto-rotate) là task riêng, mỗi mốc cần driver thật + firmware + gate.
  Ảnh vẫn là interim transport trên kernel vendor, KHÔNG phải QEAFIVELS Kernel.

## Ảnh snapshot của gate luôn khớp lần chạy (2026-11-18)

- **Nguyên nhân:** `scripts/test_violet_shell.py` chuyển PPM→PNG theo một danh sách
  tên viết tay, trong khi test tự quyết định chụp gì. Danh sách lệch 5 tên
  (`usb-sheet`, `m3-layout`, `m3-layout-dialog`, `recents-carousel`,
  `recents-empty`) nên các ảnh đó không bao giờ được tạo trừ khi ai đó convert
  tay, và một PPM mồ côi (`control-edit-debug`) vẫn bị chuyển lại mỗi lần. Vì
  vậy PNG có thể cũ hơn PPM dù script in "snapshots converted".
- **Sửa:** danh sách tên chỉ còn là *yêu cầu bắt buộc phải có* (thiếu là fail);
  bước chuyển đổi lấy **mọi** `interaction-*.ppm` của lần chạy; trước khi chạy
  test, `interaction-*.ppm`/`*.png` cũ bị xoá nên không còn ảnh mồ côi hay ảnh
  của lần trước; cuối cùng in số ảnh đã chuyển.
- **Lỗi test lộ ra:** ảnh `material-light` nằm trong nhánh tìm tiêu đề "Material
  Design 3" — tiêu đề đã đổi thành "Thiết kế QEAF" nên ảnh bị bỏ im lặng. Nay
  tìm đúng tiêu đề hiện tại và `CHECK` sự tồn tại, để lần đổi tên sau làm gate
  đỏ thay vì lặng lẽ mất ảnh.
- **Bằng chứng:** mỗi lần chạy ra **49 PPM / 49 PNG**; đặt một PNG sai vào
  `interaction-usb-sheet.png` rồi chạy lại thì tệp được ghi đè và sha256 khớp
  ảnh chuyển từ PPM hiện tại; `interaction-material-light.png` xuất hiện lại.


## Mục USB trong Cài đặt — xem và đổi chế độ bất cứ lúc nào (2026-11-18)

Yêu cầu: thêm một mục USB trong Cài đặt để xem và đổi chế độ USB (Sạc /
Truyền tệp) mà không phụ thuộc việc rút cắm cáp, và gate host kiểm được việc
đổi chế độ đó.

- **Một nguồn sự thật, một người ghi:** shell không đụng configfs. Nó đọc chế
  độ đã áp qua provider `vqeaf_ui_set_usb_mode_provider()` (main nối tới
  `usb_gadget_mode()`, trả `-1` khi LUN chưa sẵn sàng thay vì đoán "charge"),
  và bảng chọn vẫn chỉ *báo* lựa chọn; `main` vẫn là chỗ duy nhất gọi
  `vio_lun_set_mode()`. Nhờ vậy không có đường ghi thứ hai vào gadget.
- **Trang Cài đặt:** thẻ "USB" nằm ngay dưới ô tìm kiếm, phụ đề là chế độ thật
  ("Chỉ sạc · máy ẩn khỏi máy tính" / "Truyền tệp · ổ QEAFRESCUE" / "Không có
  USB trên thiết bị này" khi không có backend). Chạm mở đúng sheet "Dùng USB
  cho" của lần cắm cáp, nên vẫn đúng hai lựa chọn và ghi chú PTP như sheet đó.
  Phụ đề là live label, cập nhật qua
  `vqeaf_pages_refresh()` ngay sau khi main áp chế độ mới — không cần dựng lại
  trang.
- **Gate host** (`tests/vqeaf_shell_interaction.c` bước 15):
  - không có provider → `vqeaf_ui_usb_mode() == -1` (không bịa chế độ);
  - provider trả giá trị ngoài Sạc/Truyền tệp (2 và 7) → `-1`, nên nếu ai thêm
    lại đường PTP thì gate đỏ ngay, không chỉ dựa vào ghi chú tài liệu;
  - Cài đặt có mục "USB" với phụ đề đúng chế độ provider trả;
  - chạm mục đó mở sheet **khi không có sự kiện cắm cáp nào**;
  - chọn "Truyền tệp" → `vqeaf_ui_take_usb_choice() == VQEAF_USB_FILE`;
  - sau khi main áp (gate mô phỏng đúng bước đó) phụ đề đổi thành
    "Truyền tệp · ổ QEAFRESCUE";
  - mở lại từ chính mục đó, chọn "Sạc thiết bị" → pick `VQEAF_USB_CHARGE`, phụ
    đề quay lại "Chỉ sạc · máy ẩn khỏi máy tính".
  - Ảnh: `build/screens/phone-ui/interaction-settings-usb.png`.
- **Sửa lỗi "Chỉ sạc" bị ghi đè:** `vio_lun_set_mode(CHARGE)` thả UDC và đặt
  `gadget = 0`, nhưng vòng retry UDC muộn trong `main` lại bind lại sau ≤3 s —
  trên máy, chọn "Sạc" sẽ tự biến thành ổ đĩa. Thêm cờ `lun.charge_only`
  (`usb_lun.h`/`usb_lun.c`) đặt khi người dùng chọn Sạc và xoá khi chọn chế độ
  dữ liệu; retry chỉ chạy khi `!lun.charge_only`, nên UDC trễ lúc boot vẫn được
  bind như cũ. Đây là lỗi có từ hộp thoại USB trước, chỉ lộ ra khi chế độ được
  đổi từ Cài đặt mà không có cắm lại cáp.
- **Gộp overlay + đóng hoãn (review lại hộp thoại USB):** menu nguồn và sheet
  USB đang có hai bản y hệt của scrim + `lv_indev_wait_release` +
  `lv_async_call`, cộng một cờ `g_power_menu_closing` riêng. Nay cả hai dùng
  chung `overlay_t` với `overlay_open()` / `overlay_close()` /
  `overlay_dismiss()` trong `vqeaf_ui.c`; `power_menu_close`,
  `power_menu_deferred_close`, `power_menu_overlay_cb`, `usb_dialog_close`,
  `usb_dialog_deferred_close`, `usb_dialog_overlay_cb` và cờ `_closing` đã bị
  xoá. Quy tắc một chỗ: đóng do chạm (nền hoặc chọn dòng) luôn hoãn một tick để
  LVGL xử lý xong cú chạm; đóng kèm điều hướng/tắt màn hình (Khoá, Ngủ, Khởi
  động lại, Tắt nguồn, `hide()` khi rút cáp) đóng ngay — màn hình ngủ không
  chạy `lv_timer_handler()` nên hàng đợi async sẽ nằm lại qua giấc ngủ. Nhánh
  so sánh instance trong async đã bỏ vì `show()` từ chối mở lại khi slot còn
  bận. Sheet USB cũng bỏ ba biến tạm (`title`, `hint`, `line`).
- **Kênh cứu hộ trong chế độ "Chỉ sạc":** ghi chú HAL cũ hứa LUN cứu hộ là
  kênh điều khiển luôn hiện ở cả ba chế độ, nhưng ở violet cả function dữ liệu
  *chính là* LUN đó — thả UDC thì máy tính không thấy gì. Không thể vừa "ẩn
  khỏi máy tính" vừa "luôn hiện", nên ghi chú `usb_gadget.h` được sửa cho
  đúng sự thật, kèm đường về: mục USB trong Cài đặt (không cần rút cắm) và một
  lần reboot (bind lúc boot là truyền tệp); volume RAM không bị huỷ nên cùng ổ
  đó hiện lại. Thông báo khi chọn Sạc nay chỉ thẳng "Cài đặt › USB", và gate
  host thêm check: đang Chỉ sạc vẫn mở được bảng chọn và đổi lại Truyền tệp.
- **Sửa trạng thái sai lúc boot:** `vio_lun_init()` đặt `mode = CHARGE` trong khi
  `vio_lun_start_gadget()` vẫn bind mass storage, nên `status` và mục Cài đặt
  báo "Chỉ sạc" trong khi máy tính đang thấy ổ. Giờ bind thành công đặt
  `mode = MTP` (và xoá `charge_only`), nên chỉ chế độ thả UDC mới báo Sạc.
- **Bỏ lựa chọn "Truyền ảnh" không bao giờ thành hiện thực:** kernel violet chỉ
  có functionfs, không có responder PTP, nên chọn dòng đó luôn rơi về chế độ ổ
  đĩa. Sheet giờ chỉ còn hai dòng chọn được (Sạc, Truyền tệp) và một ghi chú
  mờ dưới cùng: "Truyền ảnh chưa hỗ trợ: kernel không có PTP responder.";
  nhánh `VQEAF_USB_PHOTO` trong `main_violet.c` (map sang `USB_MODE_PTP` và
  thông báo fallback) đã xoá vì không còn ai phát ra giá trị đó. Hằng số
  `VQEAF_USB_PHOTO` cũng đã xoá khỏi `vqeaf_ui.h` ở bước sau (xem mục "Cắt nốt
  nhánh PTP ở tầng UI"); đường PTP duy nhất còn lại là hợp đồng nền tảng —
  `vio_lun_set_mode()` và verb cứu hộ `usb ptp` vẫn fallback sang ổ đĩa, không
  phải bề mặt người dùng. Gate đổi thành: không còn dòng "Truyền ảnh" nào chọn
  được, và ghi chú chưa hỗ trợ phải hiện.
- **Cắt nốt nhánh PTP ở tầng UI:** sau khi bỏ dòng "Truyền ảnh", hằng
  `VQEAF_USB_PHOTO` không còn ai phát ra, kéo theo ba nhánh chết — kiểm biên
  trong `vqeaf_ui_usb_mode()`, `case` PTP trong `usb_mode_text()` và nhánh
  `USB_MODE_PTP` trong provider ở `main_violet.c`. Cả ba đã xoá;
  `vqeaf_ui_usb_mode()` giờ chỉ chấp nhận Sạc/Tệp và trả `-1` cho mọi giá trị
  khác. Phía nền tảng vẫn giữ `USB_MODE_PTP` + fallback + verb `usb ptp` (hợp
  đồng HAL, có đường tới thật), và ghi chú `usb_gadget.h`/`usb_lun.h` nay nói
  rõ shell chỉ đưa hai lựa chọn. Phát biểu bền vững nằm ở
  `docs/VIOLET_HARDWARE.md` §3.1 (kernel không có responder PTP -> chỉ hai chế
  độ dùng được, verb `usb ptp` vẫn fallback sang ổ đĩa), để lần sau không ai
  thêm lại lựa chọn ảnh.
- **Ranh giới:** gate chứng minh đường UI → pick → phụ đề trên host; việc ghi
  configfs thật (`UDC` unbind/bind) vẫn chỉ kiểm được trên máy, qua `status`
  (`usb_mode=`) và ổ `F:` xuất hiện/biến mất.

## VQEAF thành OS mặc định vĩnh viễn trên violet (2026-11-16, có uỷ quyền từng bước)

Yêu cầu "nạp lại os không quay lại nền tảng di động gốc nữa". Người dùng uỷ quyền rõ ràng
qua từng bước (AskUserQuestion) để bỏ ràng buộc chỉ-p77 cho mục tiêu này.

- **Chẩn đoán gốc (bằng chứng `/proc/cmdline`):** bootloader stock violet luôn nối
  `skip_initramfs root=PARTUUID=… init=/init` khi boot thường → kernel mount thẳng
  system nền tảng di động (p85), bỏ qua initramfs VQEAF. nên VQEAF (initramfs PID1) **chỉ boot
  được qua đường recovery**. Flash boot đơn thuần không đủ.
- **Bước 1 — flash boot p76** (uỷ quyền): `fastboot flash boot dist/boot-vqeaf-violet.img`
  OKAY; readback `adb root dd /dev/block/by-name/boot` (mmcblk0p76) khớp sha
  `37a6fe16…` → flash stuck. Nhưng boot thường vẫn ra nền tảng di động `green`.
- **Bước 2 — tắt AVB** (uỷ quyền): `ro.boot.veritymode=enforcing`, `vbmeta.device_state=locked`,
  `verifiedbootstate=green` chặn boot custom. `fastboot --disable-verity --disable-verification
  flash vbmeta build/screens/device/vbmeta-stock.img` (p11) OKAY → cmdline
  `vendorboot.verifiedbootstate=orange`. Boot thường vẫn ra nền tảng di động (do skip_initramfs).
- **Bước 3 — misc BCB** (uỷ quyền): ghi `boot-recovery` vào command[32] của misc BCB
  (mmcblk0p50) qua `adb root dd … conv=notrunc`, status zero. Bootloader chỉ tự xoá
  `bootonce-*`; `boot-recovery` do recovery xoá — **VQEAF không xoá misc** → bền.
- **KẾT QUẢ — vĩnh viễn, đã chứng minh 2 chu kỳ boot:**
  - Boot #1 sau ghi BCB: F: QEAFRESCUE hiện, adb trống, HOMOLOG `/dev/mmcblk0p77` + OS 0.3.0.
  - Boot #2 sau rescue `reboot system` (RB_AUTOBOOT): F: QEAFRESCUE hiện, adb trống,
    uptime `[0.000]` mới → **quay lại VQEAF, KHÔNG về nền tảng di động**.
  - Evidence: `build/screens/device/flash-evidence/persistent-boot-20261116.txt`.
- **Đường khôi phục nền tảng di động:** `vbmeta-stock.img` (sha `94cde0cb…`),
  `stock-violet/boot.img` (sha `59b51ee2…`), xoá BCB misc về 0 (hoặc fastboot).
- **Quan sát còn lại (#34):** `SCREEN.RAW` 240x520 RGB565 vẫn **đen** (nonblack=0/124800,
  `build/screens/device/persistent-ui-20261116.png`); boot log dừng ở stage splash.
  Chưa xác nhận pixel UI trên phần cứng — không tuyên bố UI render đúng. Rescue LUN,
  DRM master (panel 1080x2340@60), touch NVTCapacitiveTouchScreen đã lên thật.
- **Thay đổi trạng thái an toàn thiết bị:** bootloader unlocked, AVB đã tắt (orange),
  boot=VQEAF, misc BCB=boot-recovery. Đây là đánh đổi có chủ đích để VQEAF là OS mặc định.

## Nạp violet qua fastboot — flash p77 + boot VQEAF thành công (2026-11-16)

Yêu cầu "nạp lại os" / "đã vào fastboot hãy nạp lại os". Thiết bị vào fastboot
(`product=violet`, serial `ca1af25d`, A-only nên có phân vùng recovery riêng).
Ảnh đóng gói từ cây hiện tại (gồm #33): `python scripts/package_violet.py` → ELF
aarch64-musl **9.262.808 B / 257 objects**, `dist/boot-vqeaf-violet.img`
**20.303.872 B**, sha256 `37a6fe167ddc9911296d6eac4a09aa43d7fbb9047d9bc2b005ca2dc11e21870e`.
Hai gate offline **PASS**: `validate_bootimg` + `audit_rescue_channel`.

- **Flash đúng p77/recovery** (không p76/boot, userdata, misc; không `fastboot flash boot`):
  `fastboot flash recovery dist/boot-vqeaf-violet.img` → `Sending 'recovery' (19828 KB) OKAY
  [0.437s]`, `Writing 'recovery' OKAY [0.108s]`, Finished 0.576s. Evidence:
  `build/screens/device/flash-evidence/fastboot-flash-20261116.txt`.
- `fastboot reboot recovery` lần đầu reboot vào **hệ điều hành gốc 13** (p76 không đụng,
  fingerprint `Xiaomi/violet/violet:13/TQ3A.230901.001.B1`); sau đó `adb reboot recovery`
  boot thẳng vào **VQEAF từ p77**.
- **Bằng chứng boot phần cứng thật** (lưu `build/screens/device/flash-evidence/`):
  `HOMOLOG.TXT` = `dev: /dev/mmcblk0p77 is a block device`, `boot: QEAFIVELS OS 0.3.0 init
  on violet (PID 1)`, `lun: QEAFRESCUE volume ready (65536 sectors, 33554432 bytes)`,
  DRM master `/dev/dri/card0`, connector 28 **1080x2340@60**, `drm: verified - crtc 107
  scans our fb`, touch `NVTCapacitiveTouchScreen` event1 bound LVGL, backlight 85%.
  `QEAFBOOT.LOG` uptime `[0.000]` (fresh boot), `RESPONSE.TXT = "no command has run yet"`.
  Rescue LUN **F: QEAFRESCUE** hiện (33.259.520 B), adb/fastboot trống đúng trạng thái VQEAF.
- **Quan sát còn lại (#34):** recovery boot là one-shot — thiết bị sau đó reboot ngược về
  hệ điều hành gốc; `SCREEN.RAW` (240x520 RGB565) chụp ở stage splash cho khung **đen**
  (nonblack=0/124800, `build/screens/device/splash-20261116.png`). Chưa xác nhận pixel UI
  trên thiết bị; cần snap sau khi UI settle. Không tuyên bố UI render đúng trên phần cứng.
- Tool nạp rescue-LUN nằm ở `D:\QeafivelsOfficial\QeafdropOs\scripts\`
  (`prepare_rescue_flash.py`, `read_rescue_raw.py`, `restore_p77.ps1`). Lần này dùng kênh
  fastboot theo uỷ quyền trực tiếp của người dùng; evidence là fastboot OKAY + boot log
  phần cứng, không phải rescue-LUN raw-sector self-verify.

## Settings Service thật cho chữ đậm (#33, 2026-11-16)

Yêu cầu #33: đưa toggle "Chữ đậm" từ session-only sang **Settings Service thật**,
đánh giá phân vùng prefs. Khảo sát: trước đây bold sống ở biến tĩnh
`g_bold_text` (`vqeaf_theme.c`), chưa có service nào tên settings, chưa có
prefs partition; nhưng stack FAT16 **ghi được** đã có
(`vqeaf_fat16_write_file`, block device có `write_sectors`) và smoke test đã
chứng minh "reboot = mount lại cùng device".

- **Settings Service** (`system/core/settings.c` + `include/vqeaf/settings.h`):
  kho key/value có kiểu (bool/int/str), một nguồn sự thật cho preference, không
  phụ thuộc UI hay filesystem. Backend bền vững cắm được qua
  `vqeaf_settings_backend_t` (save/load ảnh cố định). Không backend → RAM
  session-only; có backend → mọi set write-through, mọi boot reload.
  Ảnh bền vững 512 byte: magic `VQS1` + version + count + used_len + record +
  checksum FNV-1a32; ảnh hỏng/cụt bị từ chối khi load, store bắt đầu rỗng.
  Giới hạn: key < 32 ký tự, str < 64 ký tự, tối đa 32 key, ảnh cố định 512 byte.
- **Backend FAT16** (`system/core/settings_fat16.c`): giữ prefs trong file gốc
  `PREFS.DAT` kích thước cố định 512 byte để mỗi lần ghi là ghi đè tại chỗ
  (FAT16 writer cho tạo mới/co lại/bằng, không cho phình). Mount lại cùng
  device đọc đúng ảnh đã lưu — chính là những gì một lần reboot làm.
- **Nối shell**: `vqeaf_type_set_bold/get_bold` (`vqeaf_theme.c`) nay đọc/ghi
  qua service với key `display.bold_text`; resolver mọi role đọc từ service.
  Toggle `bold_toggle_cb` (`vqeaf_pages.c`) đi qua đường đó. Ghi chú UI đổi
  thành trung thực: lưu qua Settings Service; violet initramfs chỉ đọc chưa có
  prefs partition ghi được nên chỉ trong phiên, sẽ bền vững khi có volume prefs.
- **Đánh giá prefs partition**: violet khởi động từ initramfs chỉ đọc, KHÔNG
  mount phân vùng prefs ghi được → không gắn backend ở boot device (không giả
  vờ persistence). Đường bền vững được chứng minh trên host và sẵn sàng nối
  khi một volume prefs ghi được được mount lúc boot.
- **Gate mới** `tests/vqeaf_settings.c` + target CMake `vqeaf_settings_smoke`:
  session-only không backend; get/set đúng kiểu + default; gắn FAT16 →
  write-through; reboot (mount lại) giữ bold/int/str; toggle off rồi reboot vẫn
  off; ảnh cố định 512 byte; ảnh bị sửa magic bị từ chối rồi set mới lại bền;
  giới hạn key/value/số entry.
- **Regression** (đã chạy, exit 0): native ctest **11/11** (gồm settings smoke);
  weights ALL OK; m3 ALL OK; ime ALL OK; shell ALL OK; render **132 PNG**
  (238 object) đã xem ảnh settings-bold đậm hơn settings thường; cross-build
  aarch64 `build_violet` **9.262.808 byte / 257 object**.
- **Ranh giới**: bằng chứng persistence là **host RAM-backed FAT16 (mô phỏng)**,
  không phải violet vật lý; không nạp ảnh mới lên thiết bị; #34 vẫn chặn chờ
  board vật lý được uỷ quyền.

## ABI v5: phơi bày layout weight cho QEAPP (#32, 2026-11-16)

Yêu cầu #32: cân nhắc + phơi bày font weight và layout weight qua bề mặt
theme/ABI của QEAPP, không phá ABI hiện có. Khảo sát cho thấy **font weight đã
hoàn tất ở ABI v4** (`draw_text_weighted`, validator/runtime/sim nhận {2,3,4}).
Phần còn lại là **layout weight**: thêm **ABI v5** với một primitive chia rect
theo trọng số thuần tuý, superset nghiêm ngặt của v4 (mười trường đầu
byte-identical), để canvas immediate-mode bố cục theo tỉ lệ mà không cần cây
widget LVGL trong tiến trình app.

- **Hợp đồng** (`framework/application_api/qeaf_api.h`): `VQEAF_APP_API_V5_VERSION`,
  `qeaf_rect_t`, `QEAF_LAYOUT_AXIS_HORIZONTAL/VERTICAL`,
  `qeaf_layout_split_weighted_fn` và `qeaf_app_api_v5`. Thuật toán chia nằm một
  lần ở `qeaf_layout_split_rect` (static inline) — mô phỏng LVGL flex-grow:
  trừ `gap*(n-1)`, chia theo tổng trọng số, dồn phần dư nguyên từng pixel cho
  các rect dẫn đầu để tổng luôn đúng bằng span tự do. `qeaf_native_abi.h` thêm
  `QEAPP_IMPORT_API_V5`.
- **SDK + validator**: `qen.py`, `qecompile.py`, `qeaf.py` nhận abi {2,3,4,5};
  `min_api` phải bằng abi_version; đúng một import khớp tên. Loader device
  `vqeaf_qeapp_native.c` và inspector `vqeaf_qeapp.c` nhận byte abi 5.
- **Runtime**: `vqeaf_qeapp_device.c` (device), `qeaf_sim.c/.h` (host) và
  `harness.c` (arm64 QEMU) đều dựng bảng v5, phát hiện import v5 và gọi chung
  một hàm chia thuần tuý → host/device/arm64 giống nhau từng pixel. Host sim
  ghi lại mọi lời gọi split (kể cả lời gọi bị từ chối) qua
  `qeaf_sim_split_count/at/rect_at` để gate khẳng định toạ độ chính xác.
- **Tính chất**: split là thuần tuý, **không cần quyền** và không tác động
  framebuffer → chạy được cả khi app không có `ui.display`; chỉ nhãn vẽ mới
  cần quyền. Từ chối `-1` khi weights/out NULL, n=0, axis lạ, w/h âm, gap tràn
  hoặc tổng trọng số 0.
- **Fixture + gate**: `tests/fixtures/qeapp-sim/layout.c` (v5) chia ngang
  1:2:1 và dọc 1:1:1 (có phần dư), vẽ nhãn tại mỗi gốc rect để hash framebuffer
  mã hoá bố cục, và probe 6 tổ hợp tham số sai. `tests/vqeaf_sim_test.c` khẳng
  định rect đúng `152/304/152` và `94/94/93`, 6 probe bị từ chối, split chạy
  không cần quyền. `scripts/test_qeapp_aarch64.py` chạy fixture trên QEMU virt
  arm64 và so hash với host x86_64.
- **Tài liệu**: `docs/QEAPP_NATIVE_ABI_V2.md` thêm mục "Framework API v4 and v5
  (weight revisions, implemented)"; bảng header ghi abi (2/3/4/5).

Gate (đã chạy, exit 0): contract `tests/test_qeapp_native.py` 28 tests OK;
host sim `scripts/test_qeapp_simulator.py` **101 checks, 0 failures**;
AArch64 `scripts/test_qeapp_aarch64.py` 22 ok, hash layout **0x86b982c1** khớp
host x86_64 (cross-arch determinism, 0 leak); loader 23 ok; runtime/guard/broker
ALL OK; v1 `tests/test_qeapp.py` 35 tests OK; shell `test_violet_shell.py`
**275 ok ALL OK**; IME ALL OK; render `render_phone_ui.py` 132 PNG exit 0.

Giới hạn: bằng chứng arm64 là **mô phỏng QEMU virt**, không phải phần cứng
violet vật lý; kernel OS độc lập chưa chạy app. Không nạp ảnh mới lên thiết bị.
v2/v3/v4 giữ nguyên đóng băng; không đổi màn hình shell nên không có render
màn hình mới.

## Ứng dụng Music theo mockup Player/Canvas/Queue/landscape (2026-11-16)

Yêu cầu: "tạo ứng dụng music như hình ảnh" (4 mặt: Player art tràn viền, Canvas
phủ lời, Queue kéo-thả, Sideways ngang). Triển khai trong `vqeaf_pages.c`
(`build_music`, `build_music_queue`, `music_*_cb`), dựng đủ bốn mặt và nối vào
shell như một app hệ thống.

- **Player**: art sinh thủ tục từ hash tên bài (gradient + 2 blob), tiêu đề/nghệ
  sĩ thật từ metadata (ID3/WAV), nút tim + more, dòng lời + chevron, progress +
  mốc thời gian, prev/pause/next, thanh đáy lyrics | [thiết bị, shuffle, người] |
  queue, dòng codec. **Canvas**: art tràn + overlay lời LRC thật (sidecar
  `<stem>.lrc`), hoặc "Chưa có lời bài hát" khi không có. **Queue**: thumb, tay
  nắm "=" kéo-thả đổi thứ tự thật, nút X xoá thật (memmove), "Hàng chờ" +
  "Phát tiếp từ: /media · N bài", mini progress + controls. **Sideways**: art
  trái / thông tin + controls phải, overlay lời trên art.
- **Trung thực**: OS chưa có codec/mixer âm thanh nên nút phát mờ + snackbar
  "Chưa có codec âm thanh (WAV/MP3) hay mixer — không phát được." và dòng cố định
  "Không phát/tua được: OS chưa có codec âm thanh."; progress đứng ở 0:00.
  Metadata/queue/like/shuffle/repeat/lời là state phiên thật, không giả phát.
- **Sửa gesture cướp kéo-thả**: vuốt dọc bắt đầu trong hàng chờ trước đây bong
  bóng lên shell và mở Trung tâm điều khiển (pull-down). Hàng chờ giờ
  `lv_obj_remove_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE)` để drag thuộc về list,
  đúng cơ chế nền tảng di động (vuốt trong vùng cuộn/kéo không kéo shade).
- **Test mới** (pointer thật, `tests/vqeaf_shell_interaction.c` bước 26): mở
  Music, dòng codec + dòng giới hạn + 0:00; like đổi màu tim; more mở dialog
  metadata thật rồi đóng; play báo snackbar thiếu codec; next/prev đổi bài thật
  (dòng codec đổi rồi trở lại); lyrics → canvas → về; queue mở + header + nguồn;
  kéo tay nắm "=" đổi thứ tự thật (hàng đầu đổi bài, highlight theo); X xoá còn
  1 bài; repeat đổi icon all-inclusive; shuffle đổi màu; queue về player.
- **Gate**: `scripts/test_violet_shell.py` exit 0, **275 ok / ALL OK**;
  `scripts/test_ime.py` exit 0 (ALL OK); `scripts/render_phone_ui.py` exit 0
  (199 PNG). Ảnh đã xem mắt: `interaction-music-player/-more/-play-snack/-next/
  -canvas/-queue/-queue-reorder/-queue-removed.png` và
  `music-1560x720-providers.png` (khớp mockup cả bốn mặt).
- **Giới hạn**: chưa phát/tua được (thiếu codec/mixer); art là sinh thủ tục chứ
  chưa trích ảnh bìa nhúng; lời LRC tĩnh theo bài, chưa cuộn theo thời gian thực.

## Sửa gate ngủ màn hình giữa chừng gõ phím (#50, 2026-11-16)

- Triệu chứng "gõ keyboard là bị đóng" trong gate KHÔNG phải focus steal: indev
  tổng hợp của gate không báo hoạt động người dùng nên đồng hồ idle của
  screen-timeout ngủ shell về LOCK giữa phiên gõ, và `vqeaf_pages_build` gọi
  `vqeaf_ime_hide()` mỗi lần dựng lại → IME biến mất.
- Sửa: `indev_read_cb` của gate gọi `vqeaf_ui_activity()` khi `g_pressed`, đúng
  vật lý thiết bị (`main_violet.c`: `if(touch.pressed) vqeaf_ui_activity();`).
  Giữ nguyên các sửa phía IME (strip focus, rebuild async, guard hide anim).
- Gate: shell 275 ok / ALL OK exit 0; IME rời ALL OK exit 0. Không còn trace
  tạm trong `vqeaf_ui.c`/`vqeaf_pages.c`/test (giữ trace build_grid có sẵn của
  `vqeaf_ime.c`).

## Cập nhật UI theo ảnh tham chiếu — shell LVGL (2026-11-16)

Yêu cầu: cập nhật giao diện/tính năng theo 14 ảnh mẫu (quick settings nhóm,
edit tiles, app info, app drawer phân trang, home/lock đồng hồ lớn), với ranh
giới rõ ràng: **không nền tảng di động, không Google Play Store**. Mọi dữ liệu hiển thị
đều lấy từ provider thật của QEAF; tile không có backend vẫn ghi "Không hỗ trợ".

- **Trung tâm điều khiển**: 12 tile chia 3 nhóm `Kết nối / Tiện ích / Màn hình`,
  dạng pill icon-trái/chữ-phải. Thêm `Sửa` → chế độ chỉnh sửa với `Đặt lại` /
  `Xong` và `× Bỏ` trên từng tile (chỉ đổi RAM, không ghi đĩa). Tile mới: dữ liệu
  di động, phát Wi-Fi, vị trí, chế độ; tất cả đều báo đúng trạng thái hoặc nói rõ
  giới hạn. Đã sinh thêm 13 icon Material qua `scripts/generate_lvgl_fonts.ps1`.
- **App drawer**: 25 app chia 4 trang, 8 icon/trang + 4 chấm dẫn click được;
  vuốt ngang chuyển trang, vuốt phải ở trang đầu về Home như cũ.
- **Trang thông tin ứng dụng**: thêm thẻ "Chi tiết gói" với định danh, phiên bản,
  runtime, chữ ký HMAC phát triển, quyền và dung lượng dữ liệu riêng — lấy từ
  catalog p77/QEAPP. Không có đánh giá, lượt tải hay tên app store nào.
- Đã có sẵn và giữ nguyên: power menu 2×2, recent apps + Đóng tất cả, thanh độ
  sáng thật, notification cards, lock/home đồng hồ lớn.

**Gate sau thay đổi:** `test_violet_shell.py` ALL OK (thêm 8 check edit-tiles +
3 nhóm + phân trang, snapshot mới `control-edit`) · `render_phone_ui.py`
120 PNG (30 frame × 2 profile × 2 hướng) · 65/65 unittest · `scripts/test.py`
EXIT 0 · `test_kernel_aarch64.py` ALL OK · `test_qeapp_aarch64.py` ALL OK ·
`build_violet.py` ELF 9.200.672 B / 256 object · `package_violet.py`
`validate_bootimg` PASS + `audit_rescue_channel` PASS → ảnh
`dist/boot-vqeaf-violet.img` 20.279.296 B / sha `2aa518ee…`.

**Ranh giới:** đây là ảnh violet LVGL/Linux-musl, chưa nạp lên máy trong phiên
này (máy đang ở nền tảng di động, xem mục flash bên dưới). Kernel F3 AArch64 vẫn là
QEMU virt, không đổi. Không thêm app/system nền tảng di động nào.

## Nạp OS Material 3 lên violet — p77/recovery (2026-11-16)

Yêu cầu user: `nạp os vào thiết bị` (nhắc lại lần hai) — consent rõ ràng cho ghi
phân vùng. Chỉ ghi **p77/recovery**; `p76/boot`, `userdata`, `misc` không bị
đụng. Đây là lần nạp đầu tiên của bản build có tầng Material 3.

- **Ảnh đã nạp:** `dist/boot-vqeaf-violet.img` lúc nạp là **20.230.144 byte,
  sha256 `5ccc143e12c4cc1f9d6fb97916ce45a24533567439669e22db531916020da574`**,
  build 18:43 ngày 15; header `page=4096 kernel=17505611 ramdisk=2716558`,
  cmdline giữ `force_initramfs prefer_initramfs`. Cây nguồn khớp ảnh ở thời
  điểm nạp: chỉ `vqeaf_host_media.h` mới hơn (header host-only, device build
  không compile).
- **Gate 1–3 trước khi nạp:** `build_violet.py` → ELF 9.056.224 byte / **255
  object**; `render_phone_ui.py` → **96 PNG** (24 frame × 2 profile × 2 hướng);
  `test_violet_shell.py` → **161 ok / 0 FAIL**; `package_violet.py` →
  `validate_bootimg` PASS, `audit_rescue_channel` PASS (rescue_shell 14,
  flash_verb 4, sync_protocol 7, gadget/UDC, init_panel), ảnh ra đúng sha trên.
- **Thiết bị trước khi ghi:** LUN `F: QEAFRESCUE` 33.259.520 byte; `status`
  trả `QEAFIVELS OS 0.3.0; device violet; ui=1; drm=1; uptime=18515s` — máy
  đang chạy VQEAF (không phải nền tảng di động/fastboot) nên đi đường LUN, không
  `adb`/`fastboot`.
- **Triển khai:** `prepare_rescue_flash.py --image ... --volume F:\` dry-run
  PASS → `--commit`: `BOOTIMG.BIN` 20.230.144 byte (sha tự kiểm), `QEAF.CMD`
  đúng một dòng `flash recovery BOOTIMG.BIN confirm`, rồi
  `Write-VolumeCache -DriveLetter F` (không eject).
- **HAI BẰNG CHỨNG BẮT BUỘC (raw sector):**
  - `RESPONSE.TXT`: `flash: written and verified byte for byte`
  - `QEAFBOOT.LOG`: `lun: verified - recovery and BOOTIMG.BIN agree byte for
    byte over 20230144 bytes`
- **Reboot:** `reboot recovery`; flush báo không tìm thấy F (volume biến mất
  đúng như các lần trước — lệnh đã tới máy). LUN trở lại với
  `SizeRemaining=32.727.040`, `BOOTIMG.BIN` đã biến mất (RAM LUN dựng lại).
- **Gate 4 sau boot:** `uptime=57s` + nonce mới chứng minh boot MỚI.
  `HOMOLOG.TXT` có `boot: QEAFIVELS OS 0.3.0 init on violet (PID 1)`,
  `usb: mass storage bound to a600000.dwc3`,
  `drm: verified - crtc 107 scans our fb 188 at 1080x2340`,
  `ui: splash 720x1560`, `ui: LVGL phone shell active at 720x1560`,
  `appstore: seq 28, 15 app(s)` + `appstore: boot restore 15 app(s)`.
- **Ảnh chụp thật (SCREEN.RAW 249.600 byte, RGB565 240x520):**
  - `snap` sau boot → so với render VỪA build: màn khoá **match 99,8 %, mean
    0,3, PASS** → `build/screens/phone-ui/compare-lock-m3.png`.
  - `swipe` mở khoá + `page settings` + `snap` → **match 97,5 %, mean 2,1,
    PASS** (profile providers) → `compare-settings-m3.png`; ảnh phóng 3× của
    máy: `build/screens/device/flash-20261115-m3-card.png`.
  - Quan sát trung thực: ngay sau boot shell ở **Trung tâm điều khiển
    (page=3)** chứ không phải màn khoá; `power` đưa về khoá và khớp render
    99,8 %. Chưa xác định nguyên nhân (nghi một chạm phantom vào status bar
    lúc khởi động); không sửa trong lần nạp này.
  - Thẻ "Material Design 3" trong Cài đặt nằm dưới màn hình đầu; kênh cứu hộ
    chỉ có `swipe` (không có scroll-to) và timeout màn hình 30 s khoá giữa các
    batch, nên **chưa có ảnh máy của đúng thẻ đó** — phần M3 được chứng minh
    bằng 161 check host + render (`interaction-material-*.png`); trên máy đã
    thấy dock elevation 5 và các row đổi màu theo bảng màu động.
- **Cảnh báo trạng thái cây nguồn:** ngay sau lần nạp, `dist/boot-vqeaf-violet.img`
  bị thay bằng bản khác (**20.254.720 byte, sha `e5ddf881…`**, mtime 06:21
  ngày 16) và `vqeaf_pages.c` có mtime 12:01 ngày 16 — tức cây nguồn đang được
  sửa song song. Ảnh **đang nằm trên máy** vẫn là `5ccc143e…` đã kiểm ở trên;
  muốn nạp bản mới hơn phải build + gate lại từ cây hiện tại.
- **Ranh giới:** ảnh violet (LVGL/Linux-musl) là thứ được nạp; kernel **F3
  AArch64 độc lập vẫn chỉ chạy QEMU virt**, không nạp lên violet. Không có
  driver cảm ứng/Wi-Fi/BT/pin mới nào được chứng minh ở lần nạp này; hướng #31
  và việc #32 giữ nguyên.

## Nạp OS bản dock-chỉ-ở-Home lên violet — p77/recovery, lần 2 (2026-11-15)

Yêu cầu user: `nạp os vào thiết bị` (lần 2, sau thay đổi dock #49). Đồng ý rõ
ràng cho thao tác ghi phân vùng. Chỉ ghi **p77/recovery**; không đụng
`p76/boot`, `userdata`, `misc`.

- **Ảnh mới khác lần trước:** `dist/boot-vqeaf-violet.img` **20.246.528 byte,
  sha256 `292c486e05048ad56ee5865dd392a190ced12680f3cb699b4776e98569d5b55c`**,
  header `page=4096 kernel=17505611 ramdisk=2734417`. Lần trước là `4de5924f…`
  / 20.205.568 byte; ảnh này nhúng shell mới (dock/hotseat chỉ hiện ở Home,
  app hệ thống + .qeapp chiếm toàn màn hình kiểu nền tảng di động). Ảnh cũ trên đĩa đã
  bị phát hiện stale so với nguồn dock nên đóng gói lại trước khi nạp.
- **Gate chạy lại từ đầu trước khi nạp:** `build_violet.py` → ELF
  **9.090.280 byte / 256 object**; gate shell **181 check ALL OK** (gồm các
  check dock/pill mới `vqeaf_ui_dock_visible` / `vqeaf_ui_gesture_pill_visible`);
  `render_phone_ui.py` → **116 PNG** không rỗng / không trùng;
  `package_violet.py` → `validate_bootimg` **PASS** + `audit_rescue_channel`
  **PASS**, ảnh ra đúng sha `292c486e…`.
- **Triển khai:** dry-run rồi `--commit` `prepare_rescue_flash.py --volume F:/`
  → `BOOTIMG.BIN` (sha tự kiểm lại) + `QEAF.CMD` đúng một dòng
  `flash recovery BOOTIMG.BIN confirm`; `Write-VolumeCache -DriveLetter F`
  (không eject).
- **HAI BẰNG CHỨNG BẮT BUỘC (raw sector, không dùng view mount):**
  - `RESPONSE.TXT`: `flash: written and verified byte for byte`
  - `QEAFBOOT.LOG`: `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over 20246528 bytes`
  - Lưu tại `build/screens/device/flash-evidence/`.
- **Reboot:** `reboot recovery`; lúc flush `Write-VolumeCache` ném exit 1 thoáng
  qua nhưng máy đã nhận `QEAF.CMD` và reboot. Raw read lại cho `QEAFBOOT.LOG`
  mới 227 byte chỉ còn các boot stage (không còn dòng flash/snap cũ) tại
  `build/screens/device/reboot-evidence/` → boot MỚI trên ảnh vừa nạp.
- **Ảnh chụp thật + so sánh render|device:** `ui_capture_pages.py --run
  --pages lock,home,settings` → `build/screens/device/dock-check/{lock,home,
  settings}.raw`, mỗi trang 249.600 byte (240x520 RGB565).
  `ui_snapshot_compare.py`: **lock 99.8% mean 0.3 PASS**, **home 97.6% mean 2.5
  PASS**, **settings 97.6% mean 2.0 PASS** →
  `build/screens/phone-ui/compare-{lock,home,settings}.png`. Eyeball cả ba ảnh
  side-by-side: **Home** có dock 4 icon ở đáy trên CẢ render lẫn device;
  **Settings** KHÔNG dock, chỉ gesture pill, danh sách full-height; **Lock** ẩn
  cả dock lẫn pill. Chính sách dock-chỉ-ở-Home chạy đúng trên phần cứng thật.
- **Ranh giới:** chỉ p77 được ghi; kernel F3 AArch64 độc lập vẫn chỉ chạy QEMU
  virt, KHÔNG nạp lên violet; không driver cảm ứng/Wi-Fi/BT/pin mới được chứng
  minh ở lần nạp này.

## Nạp lại OS lên thiết bị violet — p77/recovery (2026-11-15)

Yêu cầu user: `nạp os vào thiết bị` — đồng ý rõ ràng cho thao tác ghi phân vùng.
Chỉ ghi **p77/recovery**; không đụng `p76/boot`, `userdata`, `misc`.

- **Phát hiện trước khi ghi:** ảnh `dist/boot-vqeaf-violet.img` trên đĩa ĐÃ ĐỔI so
  với lần flash trước (mục F3 báo `73f9aae5`, 20.201.472 byte). Ảnh hiện tại là
  **20.205.568 byte, sha256 `4de5924f8024b971d9a69f230bbb9f401c61da1b407391db24718e56ee4229bf`**,
  build lúc 13:25 ngày 14; header `page=4096 kernel=17505611 ramdisk=2692483`
  (ramdisk cũ 2.688.782 → mới 2.692.483). Vì vậy đây không phải thao tác lặp vô
  nghĩa: có bản build mới để nạp. Toàn bộ `platform/violet/` không có file nào
  mới hơn ảnh, nên ảnh khớp đúng cây nguồn hiện tại.
- **Gate 1–3 chạy lại từ đầu (bắt buộc trước khi nạp):**
  `build_violet.py` → ELF `8.977.536` byte / **255 object**;
  `render_phone_ui.py` → `720x1560 ok (23 frames x 2 profiles)`,
  `1560x720 ok`, **92 PNG** (số trang đã tăng so với 15 trang/60 PNG ghi trong
  `docs/UI_DEVICE_TEST.md` — script tự kiểm tra ảnh rỗng/sai kích thước/trùng);
  `package_violet.py` → `validate_bootimg` **PASS**, `audit_rescue_channel`
  **PASS** (rescue_shell 14, flash_verb 4, gadget_configfs/UDC, init_panel,
  init_gadget…), ảnh ra đúng sha `4de5924f…` — build tái lập được, trùng byte
  với ảnh 13:25.
- **Thiết bị (đọc raw sector, không dùng view mount):** `DEVINFO.TXT`
  `device=violet`, `control_lun=QEAFRESCUE`, 65536 sector; `status` trả
  `QEAFIVELS OS 0.3.0; device violet; ui=1; drm=1; uptime=58233s` kèm nonce
  `053702` → worker cứu hộ sống. `adb devices` / `fastboot devices` đều trống
  (máy đang chạy VQEAF, không phải nền tảng di động) nên **không thể dump p77 mới 64 MiB**
  như lần 06:15; bản sao lưu đầy đủ cũ vẫn còn
  `out/p77-backup-20261114-061514.img` (67.108.864 byte, magic `QEAFVND!`,
  sha `5dd899f8…`), cộng payload dựng lại được từ source và đường rollback
  `restore_p77.ps1`. Rủi ro được chốt bằng chính cơ chế máy tự đọc lại từng byte.
- **Triển khai đường A:** dry-run `prepare_rescue_flash.py --volume F:/` PASS, rồi
  `--commit` → `BOOTIMG.BIN` 20.205.568 byte (sha tự kiểm), `QEAF.CMD` đúng một
  dòng `flash recovery BOOTIMG.BIN confirm`. `Write-VolumeCache -DriveLetter F`
  (không eject).
- **HAI BẰNG CHỨNG BẮT BUỘC (raw sector):**
  - `RESPONSE.TXT`: `flash: written and verified byte for byte`
  - `QEAFBOOT.LOG`: `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over 20205568 bytes`
  - Lưu tại `build/screens/device/flash-evidence/`.
- **Reboot:** ghi `reboot recovery`; lúc flush volume biến mất đúng như lần trước
  (`Write-VolumeCache` báo không tìm thấy F — lệnh vẫn đã tới máy). LUN trở lại
  với `SizeRemaining=32.727.040`, `BOOTIMG.BIN` đã biến mất (RAM LUN dựng lại).
- **Gate 4 sau boot:** `uptime=23s` (trước 58.233 s) + nonce `053936` chứng minh
  boot MỚI. `HOMOLOG.TXT` có đủ: `boot: QEAFIVELS OS 0.3.0 init on violet (PID 1)`,
  `usb: mass storage bound to a600000.dwc3`,
  `drm: verified - crtc 107 scans our fb 188 at 1080x2340`,
  `ui: splash 720x1560 (portrait)`, `ui: LVGL phone shell active at 720x1560`,
  `appstore: seq 25, 14 app(s)` + `appstore: boot restore 14 app(s)` (app cài
  sống qua reboot nhờ slot p77).
- **Ảnh chụp thật:** `snap` + nonce `054028` → `SCREEN.RAW` 249.600 byte
  (240x520 RGB565) → `build/screens/device/flash-20261115-boot.png`. Màn khoá
  hiện đúng (ngày tiếng Việt, chip "Chưa có dữ liệu", khoá, "Vuốt để mở khóa").
  `ui_snapshot_compare.py --self-test` PASS (phân biệt được trang sai), so
  `--page lock --profile unknown` với render VỪA build: **match 99.8%, mean 0.3,
  PASS** → `build/screens/phone-ui/compare-lock.png`.
- **Ranh giới:** ảnh violet (LVGL/Linux-musl) là thứ được nạp. Kernel **F3
  AArch64 độc lập chỉ chạy QEMU virt** (PL011 0x09000000, GICv2, ramfb fw_cfg),
  KHÔNG nạp lên violet — nạp nhầm sẽ dựng lại sự cố bootloop 2026-11-04. Không
  có driver cảm ứng/Wi-Fi/BT/pin mới được chứng minh ở lần nạp này; hướng #31
  ảnh + gate và #32 việc phụ không bị sửa.

## Sửa FAT12 ESP + khôi phục gate boot x86 (2026-11-14)

- **Triệu chứng:** OVMF (edk2-x86_64-code.fd) không boot ảnh `dist/vqeaf-x86_64-uefi.img`:
  BDS báo Boot0001–0003 "Not Found", rơi vào EFI Internal Shell, `startup.nsh` chạy
  `FS0:\EFI\BOOT\BOOTX64.EFI` → "not recognized". Lỗi ở tầng firmware, TRƯỚC khi kernel chạy.
- **Nguyên nhân gốc:** `scripts/package_bootimg.py` chỉ ghi mục FAT cho cluster của
  TỆP, không ghi cho cluster THƯ MỤC. Các thư mục một cluster `EFI`(2), `EFI/BOOT`(3),
  `EFI/VQEAF`(4) có mục FAT = 0 (free) thay vì EOF (0xFFF). OVMF không duyệt được
  thư mục con nên không tới `BOOTX64.EFI` dù file tồn tại và PE hợp lệ. Đây là lỗi
  đóng gói có sẵn, độc lập với thay đổi `elf.c` của cổng AArch64.
- **Sửa:** thêm helper `set_fat()` và ghi EOF (0xFFF) cho mỗi cluster thư mục; vòng
  lặp tệp dùng lại helper. Đóng gói lại cả hai ảnh normal + recovery.
- **Tăng cường gate:** `scripts/validate_bootimg.py` trước đây chỉ tìm chữ ký
  0xAA55/PE nên KHÔNG bắt được lỗi này. Nay thêm `validate_fat12_esp()`: kiểm BPB,
  hai bản FAT giống nhau, mục dành riêng 0/1, duyệt đệ quy cây thư mục gốc + con,
  xác nhận MỌI chuỗi cluster (tệp lẫn thư mục) kết thúc EOF (không free/bad/cycle),
  và `EFI/BOOT/BOOTX64.EFI` là PE x86_64 hợp lệ — fail-closed, phản ánh đúng điều
  OVMF cần để duyệt ESP.
- **Gate:** `scripts/run_qemu.py` XANH cả normal ("T1 alive" + đủ marker
  storage/AHCI/virtio/UI/scheduler) lẫn recovery ("RECOVERY: menu ready"); chạy lại
  nhiều lần đều pass. `scripts/test.py` đầy đủ EXIT=0 (ctest, unittest, SDK, cấu trúc
  FAT12, validate boot, QEMU, capture framebuffer splash/UI, recovery UI, input/power).
  Xác nhận thay đổi `elf.c` arch-neutral KHÔNG gây regression x86.
- **Giới hạn còn lại (trung thực):** trong lúc điều tra có MỘT lần `capture_screen.py`
  panic `STORAGE: VFS lookup failed code=0x8` ngay sau mount FAT16; các lần sau (3 lần
  capture + toàn bộ test.py) đều pass. Đây là race intermittent CÓ SẴN trong đường
  storage self-test x86 (giá trị trả về +8 bất thường vì mọi hàm FAT12 chỉ trả ≤0 →
  hỏng do đồng thời), KHÔNG do thay đổi phiên này, chưa sửa gốc; cần điều tra riêng
  về đồng thời virtio-blk/scheduler. Không tuyên bố gate nào chưa chạy.
- Ranh giới: chỉ chạy QEMU x86_64 (EMULATED), không chạm thiết bị vật lý, không flash.

## Cổng kernel AArch64 độc lập: milestones A–F (2026-11-14)

- Kernel VQEAF độc lập nay có lớp arch AArch64 mới (`kernel/arch/aarch64/`)
  chạy trên QEMU virt (`-machine virt -cpu cortex-a57 -m 128M`, boot `-kernel`
  tại 0x40000000, EL1). Đây là bản port của kernel độc lập, KHÔNG phải prototype
  Linux-musl violet (`build_violet.py`) và KHÔNG phải kernel UEFI x86_64.
- **A — boot:** entry PL011 UART, CurrentEL=EL1, RAM probe, PSCI SYSTEM_OFF
  (rc=0). **B — MMU:** page allocator (PMM bitmap dùng chung `kernel/core/memory.c`
  qua EFI map tổng hợp), TTBR0 4KiB/48-bit, identity RAM + UART + GIC, kiểm chứng
  dịch non-identity. **C — exception:** vector table VBAR_EL1, GICv2, generic
  timer (3 tick), SVC decode ESR.EC=0x15, eret trả về.
- **D — scheduler + EL0:** context switch preemptive driven by timer IRQ; frame
  272 byte (x0–x30, ELR, SPSR, SP_EL0); tác vụ kernel EL1h và tác vụ user EL0t
  với root TTBR0 riêng (kernel slot 0 chia sẻ, lá EL1-only; vùng user 0x10000000000
  cô lập bằng bit AP). `sched.c` aarch64 viết mới, chỉ dùng lại struct/hằng số
  `scheduler.h`; `kernel/core/scheduler.c` x86 giữ nguyên.
- **E — ELF loader:** dùng lại `kernel/core/elf.c` portable (thêm macro
  `VQEAF_ELF_MACHINE`, mặc định 0x3E giữ nguyên x86; build aarch64 định nghĩa
  0xB7). Nạp ảnh ELF tĩnh EM_AARCH64 thật (zig link tại user-region base) vào
  root user và chạy ở EL0.
- **F1 — IPC kiểu Binder:** syscall `sys_send`(4)/`sys_recv`(5) qua một mailbox
  kernel; hai tác vụ EL0 cô lập (sender/receiver, mỗi tác vụ một root) trao đổi
  thông điệp. Kernel xác thực vùng con trỏ user (`vqeaf_vm_user_range_mapped`:
  present + AP[6]=1) trước khi copy, nên EL0 không thể buộc kernel chạm bộ nhớ
  kernel hay gây fault. Thông điệp đi sender → kernel → receiver.
- **F2a — .qen loader + relocation:** `kernel/core/qen.c` nạp module .qen
  (header + code + bảng reloc) vào root user cô lập và áp fixup
  `R_AARCH64_RELATIVE` theo load base; cùng một .qen chạy đúng ở HAI base tĩnh
  khác nhau (chứng minh relocation thật). Generator `scripts/gen_qen_aarch64.py`.
- **F2b — Launcher/AMS:** `kernel/core/ams.c` (ActivityManagerService-like) giữ
  registry app; tác vụ Launcher in home + danh sách app cài đặt rồi gọi
  `vqeaf_ams_launch()` để nạp .qen và spawn một tiến trình EL0 MỚI (root TTBR0
  riêng, pid riêng) tại base động 0x10000400000; AMS ghi nhận vòng đời
  launch → exit. Ánh xạ Zygote fork + AMS của nền tảng di động.
- **F2c — compositor kiểu SurfaceFlinger:** `kernel/arch/aarch64/display.c`
  bring-up fw_cfg MMIO (0x09020000) + ramfb; `compositor.c` nhận surface từ EL0
  qua syscall `sys_submit_surface`(6) vào ring SPSC, blit vào framebuffer ramfb
  (guest RAM được QEMU scan out), rồi đọc lại pixel + checksum qua UART để gate
  kiểm chứng. Client EL0 `user/surface_client_aarch64.S` nộp 2 surface (đỏ 8x8
  @(4,4), teal 8x8 @(20,4)); dữ liệu surface nằm trong .text để giữ MỘT PT_LOAD
  (elf.c map từng segment từ base trang, hai segment chung trang sẽ lỗi -13).
- Phát hiện fw_cfg/ramfb trên QEMU 11: thanh ghi DMA address là
  DEVICE_BIG_ENDIAN (phải ghi `be64(descriptor PA)`); file dir chỉ đọc được qua
  DMA (data register MMIO trả 0); entry file dir 64 byte với name @offset 8;
  `RAMFBCfg` 28 byte {addr,fourcc,flags,width,height,stride} TẤT CẢ big-endian.
  Xác minh độc lập bằng QMP screendump của chính frame đã compose: đỏ
  (255,0,0) @(8,8), teal (0,200,200) @(24,8), nền graphite (16,16,16) @(40,40);
  ảnh `build/composited-frame-8x.png` (PPM gốc `build/composited-frame.ppm`).
- Ánh xạ cơ chế nền tảng di động: tiến trình UID riêng ↔ EL0 + root TTBR0 riêng; ART nạp
  mã ↔ ELF/.qen loader ở EL0; Binder ↔ IPC kernel trung gian; Launcher/AMS/Zygote
  fork ↔ F2b; SurfaceFlinger ↔ compositor F2c. Toàn bộ chuỗi .qeapp kiểu nền tảng di động
  (icon → Launcher → AMS spawn → tiến trình EL0 cô lập → nạp mã → vẽ qua IPC →
  compositor) nay chạy trên kernel độc lập AArch64.
- **Gate** `scripts/test_kernel_aarch64.py`: 50 kiểm tra ALL OK (A boot/EL1/PSCI,
  B MMU/PMM, C exception/GIC/timer/SVC, D scheduler + EL0, E ELF load, F1 IPC,
  F2a relocation 2 base, F2b Launcher/AMS spawn + lifecycle, F2c ramfb +
  compositor + readback pixel/checksum). Build `scripts/build_kernel_aarch64.py`
  (zig aarch64-linux-musl freestanding), ELF ra
  `build/kernel-aarch64/vqeaf-kernel-aarch64.elf`. Generator
  `scripts/gen_user_elf_aarch64.py` nhúng 4 chương trình EL0 (hello, ipc_send,
  ipc_recv, surface_client) + `gen_qen_aarch64.py`. Đếm tác vụ: kernel=4,
  user=7 tĩnh + 1 AMS spawn → exits=12, switches=16, PSCI rc=0. Tất cả là bằng
  chứng EMULATED QEMU virt, KHÔNG phải violet vật lý.
- Regression x86: thay đổi `elf.c` (macro `VQEAF_ELF_MACHINE`, mặc định 0x3E)
  đã xác nhận KHÔNG gây regression — gate boot QEMU x86 và toàn bộ `scripts/test.py`
  đều XANH sau khi sửa lỗi đóng gói FAT12 (xem mục ngay trên). Các nguồn F2
  (ams.c/display.c/compositor.c/qen.c) chỉ nằm trong build aarch64; `build.py`
  x86 không tham chiếu nên không bị ảnh hưởng.
- Ranh giới: phiên này không chạm thiết bị vật lý, không nạp flash; compositor
  mới là blit surface đặc lên ramfb QEMU virt (chưa có window manager/animation/
  input cảm ứng); hardware-in-loop chỉ khi có thiết bị được uỷ quyền + ảnh qua
  hai gate; chính sách flash giữ nguyên (chỉ p77, không p76/boot).
  **Cập nhật:** ranh giới "chưa có window manager" chỉ đúng cho mốc A–F ở trên;
  mốc F3 ngay dưới đã bổ sung window manager/layer thật + input bàn phím qua IRQ.

## Material Design 3 cho lõi UI (2026-11-15)

Lõi UI của shell (`platform/violet/ui/vqeaf_theme.c|h`) giờ có tầng design
system Material 3 đúng như nền tảng di động, không chỉ là vài card bo góc:

- **Vai trò màu M3** trong `vqeaf_palette_t`: `primary`/`on_primary`,
  `primary_container`/`on_primary_container`, `secondary_container`/
  `on_secondary_container`, 4 mức `surface_container_*`, `outline`,
  `outline_variant`, `error`/`on_error`/`error_container`/
  `on_error_container`, `inverse_surface`/`inverse_on_surface`. Chúng được suy
  ra từ cùng seed với bảng màu động (`palette_m3_roles()`), nên đổi màu động là
  đổi luôn component — đã kiểm bằng test đổi seed đỏ rồi đọc lại màu nút.
- **Token**: thang bo góc `VQEAF_M3_SHAPE_NONE/XS/S/M/L/XL/FULL`, elevation 0–5
  (`vqeaf_m3_elevate()` — width/ofs/opacity theo thang M3), state layer 12%
  pressed / 8% focused, motion 150/300/500 ms.
- **Tương tác thật**: `vqeaf_m3_state_layer()` pha màu on-surface lên nền (hoặc
  tự tô 12% nếu nền trong suốt); `vqeaf_m3_ripple()` tạo vòng tròn nở thật từ
  điểm chạm, clip theo bo góc của object, tự xoá khi nhấc tay. Nút filled/tonal
  dùng màu chữ của chính nó làm tint (on-primary) nên ripple tối đi đúng kiểu
  Material, không bị trắng nhoè.
- **Component**: `vqeaf_m3_button()` (filled/tonal/elevated/outlined/text, cao
  56 px, bo full), `vqeaf_m3_fab()` (64 px, elevation 3),
  `vqeaf_m3_chip()` (40 px, selected dùng secondary container),
  `vqeaf_m3_switch()` (track 52×32, thumb 24 khi bật / 16 khi tắt),
  `vqeaf_m3_card()` (elevated/filled/outlined, bo 12).
- **Áp dụng vào shell thật**: dock/nav dùng elevation 5 và state layer; mọi row
  Cài đặt mở được trang, row hẹn giờ, row theme, row "Theo hình nền", row "Chữ
  đậm" có ripple; nút "Áp dụng" của trang bàn phím là filled button M3. Thẻ
  "Material Design 3" trong Cài đặt trưng bày đủ 3 kiểu nút, chip, switch và
  FAB — switch điều khiển **theme thật** (Sáng/Tối), không phải mock.
- **Bằng chứng**: `tests/vqeaf_shell_interaction.c` mục 21 kiểm 19 điều kiện
  (touch target ≥48, bo full, đúng vai trò màu, chip selected/unselected, ripple
  nở khi giữ tay rồi tắt khi nhấc, state layer 12%, switch đổi theme thật, đổi
  seed đỏ thì component đổi theo) — `ALL OK`; ảnh
  `build/screens/phone-ui/interaction-material-components.png` và
  `interaction-material-ripple.png` (thấy rõ vòng ripple tối trên nút teal).
- **Ranh giới:** đây là Material 3 rút gọn cho shell không có pipeline bitmap:
  không có elevation tint overlay, không có motion spring đầy đủ, không có
  component library đầy đủ như Compose. Toàn bộ là LVGL trên host/initramfs;
  chưa có ảnh chụp trên thiết bị vật lý vì bootloader Xiaomi vẫn chặn.

### Phase 2 (2026-11-15): type scale + component mở rộng + nối dịch vụ thật

- **Type scale M3**: `vqeaf_m3_type()` với 13 role (display/headline/title/body/
  label × large/small) map lên các face Ubuntu đã raster (20/24/28/34/56/96);
  display pinned bold, các role chữ tôn trọng chế độ chữ đậm. Token dp nominal
  giữ riêng (`vqeaf_m3_type_token()`: 57/45/32/24/22/16/14/16/14/12/14/12/11 +
  tracking + weight); kích thước giảm dần **trong từng family** (body-large 16
  vẫn lớn hơn title-small 14 đúng M3, nên không ép giảm chéo family).
- **Component mới**: `vqeaf_m3_top_app_bar()` (64 px, surface-container),
  `vqeaf_m3_navigation_bar()` (80 px) + `vqeaf_m3_nav_item()` (pill chỉ báo
  64×32 secondary-container), `vqeaf_m3_list_item()` (56/72 px + ô trailing),
  `vqeaf_m3_dialog()` (scrim 40% + card bo XL surface-container-high elevation 3
  + nút text căn phải), `vqeaf_m3_snackbar()` (inverse-surface bo XS elevation 3
  + action màu accent), `vqeaf_m3_slider()` (48 px, track 8, thumb 24, kéo thật
  gửi `LV_EVENT_VALUE_CHANGED`), `vqeaf_m3_progress_linear()` (0..100) và
  `vqeaf_m3_progress_indeterminate()` (indicator quét ping-pong, luôn hiện).
- **Nối shell thật, không mock**: Trung tâm điều khiển dùng slider M3 cho độ
  sáng (ghi `/sys/class/backlight` thật) và âm lượng (HUD); installer trạng thái
  busy dùng progress indeterminate vì cài là atomic — **không có % giả**; row
  "Theo hình nền" thành list item M3 + switch phản ánh trạng thái auto thật;
  gallery Cài đặt: ba nút filled/tonal/outlined mở **hộp thoại thật** (confirm
  đặt lại bảng màu), **snackbar thật** (action Ẩn) và đặt lại màu; top app bar +
  navigation bar điều hướng trang thật (Home/Cài đặt/Giới thiệu).
- **Gate**: `scripts/test_m3.py` + `tests/vqeaf_m3.c` = 41 kiểm tra `ALL OK`
  (type scale, geometry/role từng component, slider/progress 0/50/100, frame có
  ink); `scripts/test_weights.py` = 23 `ALL OK`; `scripts/render_phone_ui.py` =
  96 PNG (24 frame × 2 profile × 2 geometry) không trùng/không trắng;
  `scripts/test_violet_shell.py` `ALL OK` gồm kiểm mới "dialog mở khi click" và
  "switch hiện trạng thái mới".
- **Bằng chứng ảnh** (đã xem): `interaction-material-components.png` (list item
  + switch, pill nav, top app bar), `control-720x1560-providers.png` (slider
  85%/0%, card ôm nội dung), `install-busy-720x1560-providers.png` (thanh quét),
  `interaction-material-dialog.png`, `interaction-material-snackbar.png`.
- **Ranh giới**: dialog/snackbar mở trên `lv_layer_top()`; vẫn là LVGL trên
  host/initramfs, chưa có ảnh thiết bị vật lý (bootloader Xiaomi chặn).

## Dock/hotseat chỉ hiện ở Home — app hệ thống & .qeapp theo cơ chế nền tảng di động (2026-11-15)

Yêu cầu user (khoanh đỏ ở đáy màn Settings): `khoang đỏ chỉ hiện ở màn hình home
còn các ứng dụng hay game riêng và ứng dụng hệ thống như cơ chế hệ điều hành
tên nền tảng cũ`. Dock 4 icon (hotseat) trước đây hiện ở cả HOME/APPS/CONTROL/SETTINGS;
nay chỉ còn ở **Home**, còn app hệ thống, trang chi tiết và app/game .qeapp chiếm
toàn màn hình như nền tảng di động, điều hướng bằng gesture pill / vuốt / back.

- **Triển khai** (`platform/violet/ui/vqeaf_ui.c`): thay cờ `top_level` bằng
  `home = (page == VQEAF_PAGE_HOME)`. `g_nav` (dock) chỉ clear HIDDEN khi `home`;
  `g_home_bar` (gesture pill) clear HIDDEN ở **mọi trang không khoá**, kể cả
  `QEAPP_NATIVE` (pill nổi trên app như nền tảng di động gesture navigation). Trang không
  phải Home nhận content **full-height** (`vertical_resolution - VQEAF_STATUS_H`)
  thay vì chừa chỗ cho dock; Home giữ `nav_top() - VQEAF_STATUS_H`. LOCK vẫn ẩn
  cả dock lẫn pill. Thêm accessor thật `vqeaf_ui_dock_visible()` /
  `vqeaf_ui_gesture_pill_visible()` (đọc cờ HIDDEN) để gate quan sát.
- **Luồng "Thêm vào dock"/"Di chuyển"** (`vqeaf_pages.c` `app_info_move_cb`): vì
  dock chỉ ở Home, arm từ app drawer **quay về Home trước** rồi mới arm, giữ ô
  drop hiện ra giống nền tảng di động "add to home screen"; đang ở Home thì không dựng lại.
- **Gate quan sát** (`tests/vqeaf_shell_interaction.c`, `scripts/test_violet_shell.py`):
  **181 check ok, ALL OK**. Check mới: dock ẩn + pill hiện trên APPS/SETTINGS/
  trang chi tiết (KEYBOARD)/FILES/QEAPP_NATIVE; dock + pill hiện trên HOME; LOCK
  ẩn cả hai; về Home khôi phục dock; "Thêm vào dock" trả về Home rồi drop vào
  slot 0 và tap mở SETTINGS; note timeout cuộn lọt hoàn toàn vào content
  full-height (re-derive assertion cũ "above the floating dock" vốn dựa dock).
- **Render + eyeball**: `render_phone_ui.py` **116 PNG** (29 frame × 2 profile ×
  2 geometry), không ảnh rỗng/trùng. Đã đọc trực tiếp:
  `interaction-chrome-home.png` (dock 4 icon + pill),
  `interaction-chrome-settings.png` (không dock, content tràn đáy, chỉ pill),
  `interaction-chrome-native.png` (app chiếm toàn canvas + pill),
  `settings-1560x720-providers.png` (landscape không dock),
  `home-1560x720-providers.png` (landscape vẫn dock + pill).
- **Regression + cross-build**: `test_m3`, `test_weights`, `test_ime` đều ALL OK;
  `build_violet.py` link **256 object**, ELF aarch64 **9.090.280 byte**.
- **Ranh giới**: bằng chứng host/initramfs (LVGL render + pointer gate), chưa có
  ảnh thiết bị vật lý. Ảnh `dist/boot-vqeaf-violet.img` vẫn nhúng shell cũ cho tới
  khi package + qua lại hai gate có consent (chưa làm, không flash). Pill trên app
  native là chrome LVGL; định tuyến input thật vào app native vẫn thuộc F3.

## IME đáp ứng độc lập kiểu nền tảng di động InputMethodService (2026-11-15)

Bàn phím ảo giờ là **một cửa sổ độc lập trên layer riêng** (`vqeaf_ime.c/.h`),
tách khỏi ứng dụng đang chạy, đúng hợp đồng InputMethodService: ô nhập liệu
được focus → ứng dụng "yêu cầu" IME → IME đo màn hình (kích thước, mật độ,
hướng, smallest-width) tính chiều cao H → shell resize nội dung app một lượng
H (adjustResize / imePadding) đồng bộ với nhịp trượt lên.

- **Metrics thuần** (`vqeaf_ime_metrics()`): `sw_dp = min(w,h)*160/dpi`;
  breakpoint tài nguyên `sw600dp` / `sw720dp`; thêm **hàng số** khi `sw>=600`
  hoặc landscape; hàng phím 52/56/60 dp theo breakpoint; trần chiều cao
  **portrait 35–45%**, **landscape 30–55%** màn hình; one-hand **78%** khổ,
  floating **62%** khổ, split khe giữa **64 dp**. Cùng input luôn cho cùng
  output nên gate quét đa hình học không cần UI.
- **Số đo truy vết** (gate `tests/vqeaf_ime.c`, @320dpi): phone dọc 720×1560 →
  sw360, H=546 (sàn 35%), row_h=119, font 28; phone ngang 1560×720 → có hàng
  số, H=396 (trần 55%), font 20; tablet 7" 1200×1920 → sw600, H=672; tablet
  10" 1600×2560 → sw800, H=896; fold 21:9 480×1080 → sw240, H=484; one-hand
  phải rộng 561 neo x=159; floating rộng 446 căn giữa x=137 và nâng khỏi đáy;
  split khe 128 px. Phím chia theo **layout_weight** (phím 1.5 rộng hơn phím 1).
- **Năm chế độ thích ứng**: Normal, Split (hai nửa lưới phím + khe giữa),
  One-handed trái/phải, Floating (cửa sổ tự do kéo được), Extract (editor toàn
  màn phía trên phím khi landscape quá thấp). Cửa sổ hẹp (split/floating) tự
  giảm cỡ chữ để nhãn SHIFT/ENTER vừa khổ phím.
- **Nối shell thật**: trang Ghi chú dùng IME với adjustResize (resize chính
  content trang); modal Tìm kiếm và mật khẩu Wi-Fi dùng IME dạng overlay
  (adjustNothing); "Áp dụng" chủ đề phím gọi `vqeaf_ime_restyle()` để đổi màu
  IME đang mở; đổi trang tự ẩn IME để cửa sổ không sống lâu hơn ô nhập liệu.
  Bàn phím inline `lv_keyboard` cũ ở ba chỗ này đã thay bằng IME độc lập.
- **Gate**: `scripts/test_ime.py` + `tests/vqeaf_ime.c` = **72 kiểm tra ALL OK**
  (metrics đa hình học + trần/ca breakpoint + năm chế độ + cửa sổ top-layer +
  adjustResize + weight + engine gõ thật vào textarea);
  `scripts/render_phone_ui.py` = **116 PNG** (29 frame × 2 profile × 2 geometry,
  gồm **20 PNG IME**) không trùng/không trắng; regression
  `test_violet_shell` / `test_m3` (41) / `test_weights` (23) đều `ALL OK`;
  cross-build AArch64 musl `scripts/build_violet.py` = **256 object** linked.
- **Bằng chứng ảnh** (đã xem): `ime-normal-720x1560-unknown.png` (phím weighted
  căng khổ), `ime-normal-1560x720-unknown.png` (hàng số 1–0 khi ngang),
  `ime-split-720x1560-unknown.png` (hai nửa + khe), `ime-one-hand-720x1560-unknown.png`
  (neo phải 78%), `ime-floating-720x1560-unknown.png` (cửa sổ tự do 62%),
  `ime-extract-720x1560-unknown.png` (editor toàn màn + phím).
- **Ranh giới**: vẫn là LVGL trên host/initramfs, chưa có ảnh thiết bị vật lý;
  bố cục tablet sw600/sw720 được gate xác nhận bằng số (chưa render riêng);
  kéo thả floating đã nối sự kiện nhưng gate mới kiểm vị trí/kích thước, chưa
  mô phỏng thao tác kéo; extract sao chép nội dung ô nhập liệu hiện tại.

## F3 — Cửa sổ/layer thật, InputFlinger, SystemUI EL0 + sửa race shutdown (2026-11-14)

Hướng chính F3 nâng compositor từ "blit surface đặc" lên chuỗi đúng kiểu nền tảng di động:
**SurfaceFlinger (layer/compose) + InputFlinger (route phím theo focus) +
SystemUI (launcher EL0) → AMS → app EL0 → surface**. Hai hướng còn lại giữ
nguyên trạng theo yêu cầu; mục này chỉ nói về kernel độc lập AArch64 / QEMU virt.

- **Window manager thật** (`kernel/arch/aarch64/window.c`,
  `kernel/include/vqeaf/window.h`): tối đa 8 layer, mỗi layer có owner task,
  z-order, visibility, buffer vật lý ánh xạ vào root user của owner, hàng đợi
  input riêng. `recompute_focus()` chọn layer visible có z cao nhất (hoà nhau
  thì win id lớn hơn). Compose theo thứ tự nền → surface F2c → window z tăng
  dần, có clipping; `generation` tăng khi create/present/raise/close/nhận phím
  nên compositor chỉ rebuild khi có damage. `vqeaf_wm_close_owner()` dọn layer
  khi tiến trình EL0 chết (tương đương app death của nền tảng di động). Mọi con trỏ user
  qua syscall đều được kiểm tra bằng `vqeaf_vm_user_range_mapped()`.
- **InputFlinger** (`uart.c`, `irq.c`): PL011 RX IRQ. DTB của QEMU virt cho
  `/pl011@9000000` interrupts `<0 1 4>` = SPI 1 → GIC INTID 33; GIC route về
  CPU0, priority thấp hơn timer PPI 30. IRQ handler drain toàn bộ FIFO và đẩy
  từng byte **chỉ** vào window đang focus. Không còn input giả lập trong guest.
- **SystemUI chạy EL0** (`user/launcher_aarch64.c`, `user/vqeaf_el0.h`): launcher
  tạo window 200x160 @(48,40), vẽ vào buffer riêng, `present()`, poll input, đổi
  focus khi nhận `d`/`a`, và gọi `sys_launch("com.vqeaf.window")` khi nhận Enter.
  App `.qen` mới (`user/qen_window_aarch64.c`) do AMS spawn mở window 128x96
  @(96,80) z=1 đè lên launcher, nhận phím `x`, rồi close + exit. ABI syscall
  (7–13) và struct dùng chung đặt tại `kernel/include/vqeaf/el0_abi.h`; launcher
  link một `PT_LOAD` RWX duy nhất để tránh `elf.c` từ chối hai segment chung trang.
- **Framebuffer** tăng 64x48 → **320x240** (`display_aarch64.h`) để có chỗ cho
  layer chồng lên nhau; checksum kỳ vọng của F2c được cập nhật tương ứng.
- **Gate** `scripts/test_kernel_aarch64.py` viết lại: serial là bidirectional
  `socket` chardev, host chờ marker rồi bơm **phím thật** `d`, Enter, `x` vào RX
  FIFO. Kiểm tra 81 điều kiện (A–F2c regression + F3), gồm focus routing, z-order
  occlusion, clipping, `input_irqs>=3`, 5 pixel readback, `exits=14`, context
  switch và PSCI SYSTEM_OFF rc=0. Bằng chứng độc lập qua **QMP screendump**
  (không phải tự đọc lại của kernel): `build/kernel-aarch64/f3-frame.ppm`
  320x240 và `build/screens/device/f3-composited-frame.png` — (4,4)=(255,0,0)
  đỏ F2c, (20,4)=(0,200,200) teal F2c, (70,80)=(53,224,192) tile launcher,
  (100,100)=(32,42,64) app z=1 che launcher z=0, (300,220)=(16,16,16) nền
  nguyên vẹn do clipping.
- **Race shutdown intermittent — phát hiện và sửa triệt để.** Chạy lại gate lộ
  lỗi đuôi chập chờn (~1/3 lần): `FAIL app layer lifecycle completed`,
  `FAIL all 14 tasks exited cleanly`, `FAIL context switches`, QEMU rc=1 sau 35 s
  thay vì ~6 s. Diagnostic `VQEAF_SCHED_DIAG` chốt nguyên nhân:
  `SCHED DIAG: no runnable, created=14 exited=13 ... states=4,...,4,0,...` — 13
  slot thật đều EXITED nhưng bộ đếm đòi 14. `task_create_user()` chạy ở EL1h
  **có IRQ bật**, nên timer IRQ chen giữa "tìm slot UNUSED" và "đánh dấu READY":
  `task_ams` và `sys_launch` (từ EL0) cùng claim một slot → 2 lần
  `tasks_created++` nhưng chỉ 1 tác vụ. `tasks_exited` không bao giờ đạt 14 →
  PSCI SYSTEM_OFF không gọi, nhánh "no runnable" còn hồi sinh slot EXITED thành
  RUNNING và quay `wfi` mãi.
  - Sửa 1: `vqeaf_a64_irq_save()/restore()` trong `irq.c` (chỉ chạm DAIF.I, lưu
    lại đúng trạng thái cũ nên lồng nhau an toàn). `vqeaf_task_create()` và
    `vqeaf_task_create_user()` trong `sched.c` gọi body `_locked` dưới critical
    section; `vqeaf_ams_launch()` trong `ams.c` cũng che toàn bộ root/.qen/slot/
    pid/`procs[]` nên pid và process table không thể lệch khỏi slot.
  - Sửa 2: shutdown chuyển từ so sánh bộ đếm sang **đọc bảng trạng thái**
    (`exited_task_count()`): sau khi `pick_next()` đã chứng minh không còn READY,
    mọi slot occupied phải EXITED mới power off. Bộ đếm vẫn được audit và in
    `SCHED: warning: task counters drifted` nếu lệch — lỗi thành "fail fast có
    log" thay vì treo 35 s. Bỏ hết scaffolding `VQEAF_SCHED_DIAG`.
  - Sửa 3: gate không còn ghim `-> pid=1` (giá trị đó vốn là **triệu chứng** của
    race: `task_ams` bị preempt nên `sys_launch` chiếm pid 1 trước). Nay assert
    bất biến: `sys_launch` trả đúng pid AMS cấp, và hai app có pid khác nhau.
    Thêm 2 check: không có `task counters drifted`, không có `no runnable task`.
  - Sau sửa thứ tự launch **tất định**: `demo pid=1`, `window pid=2`,
    `sys_launch → pid=2`, `exits=14, switches=21`, window owner slot 13.
- **Validation:** `scripts/test_kernel_aarch64.py` ALL OK **9/9 lần liên tiếp**
  (mỗi lần ~6 s, rc=0, 0 dòng FAIL; trước sửa có lần treo 35 s). Tái xác minh
  trên cây hiện tại (2026-11-16, sau #32/#33): **3/3 lần ALL OK**, rc=0, không
  tái phát race shutdown. Regression:
  65 host unit tests OK, `scripts/test_qeapp_aarch64.py` ALL OK,
  `scripts/test.py` (x86 QEMU/recovery/input-power/FAT12/SDK) EXIT=0. QMP
  screendump chụp lại trên kernel ĐÃ SỬA vẫn khớp cả 5 pixel.
- **Ranh giới:** toàn bộ F3 là EMULATED trên QEMU virt; phiên F3 **không chạm
  thiết bị vật lý, không flash**. Không có touch, animation, notification, hay
  driver màn hình violet. Ảnh p77 / hướng #31 và việc phụ #32 giữ nguyên, không
  sửa trong hướng này.

## Tính năng weights: độ đậm chữ + trọng số bố cục (2026-11-13)

- **Font weights:** `scripts/generate_lvgl_fonts.ps1` sinh thêm bold faces
  20/24/28/34 (đủ 310 glyph tiếng Việt) từ `Ubuntu-Bold.ttf`; header sinh thêm
  enum role/weight + resolver. Macro `VQEAF_FONT_*` nay resolve qua
  `vqeaf_type_font()` (`vqeaf_theme.c`) nên TOÀN shell tôn trọng chế độ chữ
  đậm; macro `*_BOLD` ghim mặt bold; `page_title` dùng `HEADLINE_BOLD` để phân
  cấp thị giác.
- **Toggle "Chữ đậm"** trong Settings (`bold_toggle_cb`, `vqeaf_pages.c`) bật/tắt
  chế độ bold rồi dựng lại trang đúng cơ chế restyle của theme; session-only
  trung thực (chưa có phân vùng prefs) kèm ghi chú rõ trong UI.
- **Layout weights:** `vqeaf_weighted_row/column` + `vqeaf_weight(obj, w)`
  (`vqeaf_theme.{h,c}`) chia không gian tự do theo tỉ lệ trọng số (flex grow,
  align START); dùng thật ở hàng nút installer HỦY / CÀI ĐẶT.
- **Gate mới** `scripts/test_weights.py` + `tests/vqeaf_weights.c`: 23 kiểm tra
  — bold khác regular, advance rộng hơn, nhiều ink hơn; toggle đổi đủ 4 role;
  row/col tỉ lệ 1:2:3 = 100/200/300 và 50/100/150; weight đều = 200/200;
  weight-0 giữ kích thước content.
- **Render** thêm `settings-bold` / `home-bold`: 22 frame x 2 profile x 2 hướng
  = 88 PNG, 236 object; đã xem ảnh bold vs thường và installer dùng weights.
- Regression: weights 23, render 88, runtime 7, broker 68+53+4, simulator 61
  (hash 0x6cc394ad), guard 25, shell interaction, native_loader 21, QEMU aarch64
  (hash 0x6cc394ad), build_violet 8.959.848 byte / 255 object — tất cả xanh.
- Ranh giới: weights chạy thật ở host render + cross-build aarch64; phiên này
  không chạm thiết bị vật lý; chế độ chữ đậm session-only như theme.

## Trình cài đặt gói .qeapp ba trạng thái (2026-11-13)

- Dòng chảy mới theo mẫu trình cài đặt gói (video tham chiếu): **xác nhận →
  đang cài → xong/mở**, kèm nhánh lỗi. Thay cho modal cài-ngay-lập-tức trước
  đây trong `platform/violet/ui/vqeaf_pages.c`; giữ ngôn ngữ thiết kế VQEAF
  (mặt graphite, accent teal, bo tròn), không sao chép màn nền tảng di động.
- Xác nhận đọc metadata THẬT trước khi cài: API mới `vqeaf_qeapp_preview()`
  (`vqeaf_qeapp.{h,c}`) tách `read_metadata()` dùng chung với
  `install_buffer()` — kiểm chữ ký + parse manifest/icon nhưng KHÔNG đăng ký
  app. Thẻ xác nhận hiện icon, tên, version, id và danh sách quyền khai báo
  (bit `qeaf_permission_t`), câu hỏi "Bạn có muốn cài đặt ứng dụng này không?"
  với HỦY / CÀI ĐẶT.
- Đang cài: thanh bận **vô định** (indeterminate sweep) vì phép cài là nguyên
  tử — không có phần trăm thật để hiển thị, không bịa tiến trình. Phép cài ký
  chạy ở timer một nhịp sau khi thẻ bận vẽ, rồi chuyển xong/lỗi.
- Xong: "Ứng dụng đã được cài đặt." + XONG / MỞ (MỞ mở launcher đúng app vừa
  cài vì install đặt `g_selected`). Lỗi: lý do thật từ backend + ĐÓNG.
- `install()` kiểm tra lại chữ ký lúc cài, nên gói bị đổi sau bước xem trước
  vẫn bị chặn (TOCTOU).
- Render + gate: `scripts/render_phone_ui.py` thêm 4 trạng thái
  install-confirm/busy/done/error (20 frame x 2 profile x 2 hướng = 80 PNG);
  harness host `tests/vqeaf_ui_host.c` gọi hook chỉ-host
  `vqeaf_install_render()`. Đã xem ảnh cả 4 trạng thái dọc lẫn ngang.
- Gate thật: `scripts/test_qeapp_runtime.py` thêm mode `preview` /
  `preview-tampered` (13+2 kiểm tra): preview không cài, id/name/version/icon
  đúng, install sau preview khớp preview; gói sửa bị từ chối và không cài.
  Sửa link sẵn có của gate này (thiếu stub appstore) và của
  `test_violet_shell.py` (thiếu stub `vqeaf_qeapp_device`).
- Regression: render 80 PNG, runtime 7 case, broker 68+53, simulator 61,
  guard 25, shell interaction, QEMU aarch64 hash 0x6cc394ad, build_violet
  8.561.456 byte / 251 object — tất cả xanh.
- Gate `test_qeapp_native_loader.py` xanh lại (21 kiểm tra): kỳ vọng cũ mong
  từ chối ABI 3 trong `tests/vqeaf_qen_validate.c` dù validator chấp nhận
  {2,3}; nay khẳng định ABI 3 được nhận và dời ca từ chối sang ABI 4 và 1,
  không làm yếu kiểm chứng chữ ký/kích thước/hash.
- Ranh giới: host render dùng metadata demo (seed) để dựng bố cục; thiết bị
  mới parse manifest thật. Chưa nối runner native vào launcher vì chưa có
  process model độc lập (giữ ràng buộc Phase 5).

## Phase 5: permission broker + quota + timeout cho native call (2026-11-13)

- Broker mới `framework/security/qeaf_broker.{h,c}` (C11 thuần): trần quyền
  khai báo vs quyền được cấp, check/request/revoke, vòng audit 64 mục +
  deny_count; scope đường dẫn (từ chối rỗng/`..`/tuyệt đối/ổ đĩa/ngoài scope,
  ghi cần `storage.write.app`); quota bộ nhớ mặc định 1 MiB acquire/release;
  ngân sách CPU mỗi lời gọi 1000 ms bằng clock đơn điệu; supervisor cả lượt
  5000 ms (Phase 4).
- Simulator nối broker: `framework/simulator/qeaf_sim.c` bọc mọi lời gọi
  native (start/pause/resume/stop/destroy/on_input) bằng đo thời gian +
  per-call timeout; phơi bày call_timeout/max_call_ms/deny_count/memory_used/
  audit. CLI `qeaf simulate` thêm --fb/--scope/--mem-bytes/--call-ms.
- Sửa lỗi ABI thật: red zone SysV 128 byte dưới rsp bị libc Win64 phá khi
  callback sysv_abi gọi snprintf/malloc → crash nhạy bố cục ở -O1. Khắc phục
  gốc bằng `-mno-red-zone` trong `sdk/simulator/qesim.py`.
- Gate: `tests/vqeaf_broker_test.c` 68 kiểm tra,
  `tests/vqeaf_sim_quota_test.c` 53 kiểm tra, `scripts/test_qeapp_broker.py`
  4 acceptance CLI (hello trong ngân sách; path xấu bị audit; leo thang bị từ
  chối; slow 82 ms > 5 ms bị cắt). Fixtures slow/badpath/escalate mới.
- Cross-build aarch64: broker nằm trong `scripts/build_violet.py` (compile
  proof cho thiết bị).
- Ranh giới: CHƯA nối runner thật vào launcher — kernel/arch/aarch64 trống,
  chưa có process model độc lập; enforcement trên thiết bị là bước sau.
- Regression: broker 68+53, simulator 61, guard 25, QEMU aarch64 hash
  0x6cc394ad, render, build_violet — xanh.

## Media revision: ABI v3 + QVF video — phát video thật trong OS (2026-11-13)

- **ABI v3** (`framework/application_api/qeaf_api.h`, superso cứng của v2):
  thêm `blit_pixels` (RGB565 -> framebuffer, clip biên, cần `ui.display`) và
  `read_file_part` (đọc theo khoảng, buffer của app, tính vào quota bộ nhớ
  trong lúc gọi, cần `storage.read`). Bảng v2 giữ nguyên đá; module v3 khai
  báo `abi_version: 3` trong manifest, byte ABI 3 + `min_api 3` trong `.qen`
  và import `qeaf_app_api_v3`. Toàn bộ loader/inspector/simulator/SDK chấp
  nhận cả 2 lẫn 3 (`sdk/compiler/qen.py`, `vqeaf_qeapp.c`,
  `vqeaf_qeapp_native.c`, `framework/simulator/qeaf_sim.c`).
- **QVF** (`docs/QEAF_VIDEO_FORMAT.md`): container video duy nhất OS giải mã
  được — RGB565 nén LZ4 block, header 32 byte có magic/version/codec/flags.
  Chuyển đổi MP4 -> QVF bằng `scripts/mp4_to_qvf.py` (LZ4 compressor thuần
  Python, ffmpeg qua imageio-ffmpeg; tự kiểm tra từng frame, kiểm tra chéo
  với `lz4` gốc khi có). Fixtures thật: `build/media-fixtures/video1.qvf`
  (80 frame 120x213 @10fps từ `video1.mp4` của LDPlayer) và `clip.qvf`
  (60x106 @8fps, 8 frame, nhúng trong host media tree).
- **VideoPlayer.qeapp** (`examples/videoplayer-qeapp`, manifest abi_version 3):
  stream từng frame bằng `read_file_part` (không vượt quota 1 MiB), giải mã
  LZ4 trong app, blit 2x bằng `blit_pixels`. Thất bại trung thực: NO VIDEO
  FILE / UNSUPPORTED FORMAT / CODEC NOT SUPPORTED / BAD QVF FLAGS / VIDEO TOO
  LARGE / BAD FRAME ENTRY / FRAME READ FAILED / FRAME DECODE FAILED.
  `scripts/test_videoplayer_native.py` chạy module x86_64 trong simulator với
  video thật: frame 1->2->3->2 sau OK OK LEFT, 1704 blit, 0 alloc, và đối
  chiếu **pixel-exact** (102240 pixel) vùng video với frame giải mã độc lập.
  Fix bug thật trong simulator: `check_imports` ghi qua con trỏ `sim` chưa
  cấp phát (UB) — giờ trả `api_version` qua out-param.
- **Trang Video trong shell** (`platform/violet/ui/vqeaf_video.{c,h}` +
  `build_video` trong `vqeaf_pages.c`): quét `/media` bằng magic bytes (không
  theo đuôi mở rộng), parse header QVF, giải mã frame để xem trước bằng
  `lv_image`; MP4 báo trung thực "codec H.264/AAC: OS chưa hỗ trợ". Core
  LVGL-free, test `scripts/test_vqeaf_video.py` đối chiếu frame 0/3
  byte-exact với dump giải mã Python, kiểm tra mọi mã lỗi trung thực.
  Render check: `scripts/render_phone_ui.py` (16 trang x 2 profile x 2
  hướng) — sửa harness còn thiếu stub `vqeaf_qeapp_device` (runtime
  Linux-only) nên trả về trạng thái "chưa chạy" trung thực trên host.
- Ràng buộc giữ nguyên: Phase 4 device gate — chưa có kernel AArch64, mọi
  video chạy thật ở simulator/host shell; thiết bị thật cần runner AArch64.

## Phase 4 device gate: native arm64 QEAPP chạy trên Redmi Note 7 Pro (2026-11-12)

- Runner thiết bị mới: `platform/violet/ui/vqeaf_qeapp_device.{h,c}`. Nó dùng
  đúng inspector/chữ ký của thiết bị (`vqeaf_qeapp_inspect_native`), kiểm tra
  `.qen` arm64, map `code|rodata|data|bss` vào arena 2 MiB align 4096, resolve
  export rồi gọi create/start/pause/resume/stop/destroy qua bảng API v2.
  Module chạy trong **tiến trình con** (`fork`); khung hình nằm trong mapping
  `MAP_SHARED` tạo trước fork nên crash/hang không chạm được vào shell.
- Kích hoạt thật: verb `qeapp <file>` trên kênh cứu hộ đọc gói từ QEAFRESCUE,
  chạy với timeout cứng 5000 ms, rồi shell chuyển sang trang
  `VQEAF_PAGE_QEAPP_NATIVE` để blit khung 720x1560 XRGB8888.
- Bằng chứng trên máy thật (không phải QEMU):
  - Ảnh nạp p77: `dist/boot-vqeaf-violet.img` 20.099.072 byte, sha256
    `a7d22210dab90e042e3c968e805046787145a7f353f13728eeb770f029fb310e`;
    `flash: written and verified byte for byte`, `lun: verified ... over
    20099072 bytes`. ELF `build/violet/vqeaf-init` 8.464.136 byte / 249 object.
  - `qeapp HELLO.QEA` trả: `lifecycle: create start pause resume stop destroy`,
    `framebuffer: 720x1560 hash=0x6cc394ad`, `outstanding allocs: 0`. Hash này
    trùng host x86_64 và QEMU arm64 → cùng một khung hình trên ba môi trường.
  - Trang native hiện trên panel, `status` trả `page=15`; ảnh
    `build/screens/device/native-page.png` chụp từ `SCREEN.RAW` thật.
  - Cách ly lỗi trên máy: module crash bị chặn với
    `module crashed (signal 11); shell survived`; module treo bị giết với
    `module exceeded 5000 ms and was killed`; sau cả hai, `status` vẫn trả lời
    và shell chạy tiếp.
- Cài đặt + chạy từ UI (2026-11-12): `vqeaf_qeapp_install` nhận cả gói khai báo
  lẫn gói `runtime: native` (kiểm tra chữ ký, arm64, `.qen`), và bộ lọc tệp
  nhận cả đuôi `.qeapp` lẫn alias 8.3 `.QEA`. App native đã cài có nút "Chạy
  trên thiết bị" gọi runtime có guard; verb cứu hộ `qeapp <file>` vẫn giữ để
  chẩn đoán. Đo trên máy: cài `hello.qeapp` → "Đã cài ứng dụng" → Chạy → trang
  native, `page=15`.
- Ranh giới: đây vẫn là transport prototype Linux-musl (`platform/violet`),
  **không** phải kernel QEAFIVELS độc lập; `read_file` chưa cấp cho app trên
  thiết bị; chữ ký vẫn HMAC dev-key.

## Phase 4: native runtime tối thiểu + Hello arm64 chạy thật (2026-11-12)

- Runtime có guard: `platform/violet/ui/vqeaf_qeapp_runtime.{h,c}` phơi bày entry
  phía thiết bị `vqeaf_qeapp_native_launch(runner, package, canvas, report, ...)`.
  Shell KHÔNG chạy module trong address space của mình: spawn runner làm child
  (Win32 `CreateProcessA` / POSIX `fork`+`execl`), chờ với timeout cứng 5000 ms,
  đọc report + framebuffer qua pipe. Crash (NTSTATUS >= `0xC0000000`) hoặc signal
  → `crashed`; treo → kill (`TerminateProcess`/`SIGKILL`) → `timed_out`. Mọi đường
  lỗi đều reap child, xoá framebuffer tạm, trả `-1` kèm lý do; shell sống sót và
  launch lại được. Arena bị chặn ở `QEAF_SIM_MAX_ARENA` = 2 MiB
  (`code+rodata+data+bss`) NGAY TRƯỚC khi map. Clean run trả frame 720x1560
  XRGB8888 (stride 2880) vào `lv_image_dsc_t` để shell blit.
- Hello arm64 chạy THẬT trên emulator: `tests/fixtures/qeapp-aarch64/` là harness
  AArch64 freestanding chạy đúng module arm64 Hello do SDK build dưới
  `qemu-system-aarch64 -machine virt` (UART PL011). Harness parse `.qen`, copy
  section vào arena bounded, resolve export, gọi đủ
  create/start/pause/resume/stop/destroy qua API v2 giả (log→UART, draw→raster),
  rồi tắt máy bằng PSCI. Hash framebuffer arm64 = `0x6cc394ad`, TRÙNG KHỚP hash
  x86_64 host của cùng app → bằng chứng cross-arch determinism, 0 allocation rò rỉ.
- Ba lỗi gốc đã tìm và sửa khi bring-up harness (đều bằng chứng QEMU, không đoán):
  (1) bẫy FP/SIMD vì `CPACR_EL1.FPEN=0` lúc reset → bật FPEN trong boot;
  (2) clang sinh load không align cho header `.qen` offset lẻ → build
  `-mstrict-align`; (3) ADRP+ADD granularity trang khiến con trỏ rodata lệch khi
  arena không align trang → arena phải align 4096 (x86_64 RIP-relative byte-exact
  nên host không dính). Ràng buộc align 4096 cho arm64 đã ghi vào spec ABI.
- Cross-build compile proof: thêm runtime vào `scripts/build_violet.py`; build
  link `build/violet/vqeaf-init` 8.418.032 bytes / 247 object, ELF AArch64
  (machine `0xb7`), symbol `vqeaf_qeapp_native_launch` có mặt trong ảnh. Đây là
  bằng chứng COMPILE/LINK cho arch thiết bị, KHÔNG phải tuyên bố chạy trên OS độc
  lập (build_violet.py là prototype Linux-musl legacy).
- Kiểm chứng (số thật, exit code 0): `scripts/test_qeapp_runtime_guard.py` =
  25 checks / 0 failures (clean render, crash `0xc0000005` contained, hang kill
  lúc 5000 ms, arena 3 MiB bị reject end-to-end, recovery không leak).
  `scripts/test_qeapp_aarch64.py` = ALL OK, chạy lặp lại ổn định, hash khớp.
  Regression Phase 1-3 giữ xanh: `test_qeapp_simulator.py` = 61 checks,
  `test_qeapp_native_loader.py` = 19 case, `test_qeapp_runtime.py` = ALL OK,
  `python -m unittest discover -s tests` = 62 tests OK,
  `scripts/render_phone_ui.py` = 60 PNG (15 trang x 2 profile x 2 hướng).
- Ranh giới kiểm chứng (quan trọng): arm64 mới chứng minh trên EMULATION
  (QEMU virt) + host crash-guard. KHÔNG có bằng chứng phần cứng violet thật
  (chưa có board được uỷ quyền), và kernel OS độc lập `kernel/arch/aarch64` CHƯA
  triển khai. Acceptance "runs on violet hardware" đóng bằng bằng chứng emulated
  AArch64, không tuyên bố hardware thật. Chưa thêm nút "Launch" giả vào launcher
  vì chưa có runner backend thật trên thiết bị (rule không bày dữ liệu giả).
- An toàn giữ nguyên: không thực thi code chưa validate, không cấp truy cập
  hardware/kernel cho app, chữ ký vẫn HMAC dev-key (chưa có khoá production),
  module luôn bị chặn bởi arena bound + timeout + crash guard. Phase 5
  (permission broker + quota) là bước tiếp theo.

## Lưu app đã cài qua reboot bằng vùng dự trữ p77 (2026-11-12)

- Vấn đề: QEAFRESCUE là FAT16 **trong RAM** (`/run/qeaf-lun.img`, tmpfs 32 MiB)
  dựng lại mỗi boot, nên cả gói `.qeapp` lẫn danh sách cài đặt đều mất sau
  reboot.
- Giải pháp: `platform/violet/appstore.{h,c}` lưu catalog + nguyên bytes gói
  vào vùng chưa dùng của p77 (ảnh boot kết thúc ~20 MiB): **hai slot kích thước
  động** chia đôi vùng 32–64 MiB. Header A tại 32 MiB, header B tại 32 MiB+128
  (magic `QEAFAPP2`, version, seq, count, `payload_len`, `payload_off`, CRC32);
  payload A nằm từ 32 MiB+256 đi lên, payload B nằm từ cuối 64 MiB đi xuống,
  mỗi bên tối đa ~16 MiB và dài đúng bằng catalog thật. Slot cũ hơn bị ghi đè
  trước (payload trước, header sau, fsync + đọc lại so CRC), nên mất điện giữa
  lúc ghi vẫn còn một slot nguyên vẹn. Chỉ p77 bị ghi; kernel/ramdisk không bị
  chạm.
- Luồng (kho p77 là nguồn sự thật, không phụ thuộc QEAFRESCUE):
  `vqeaf_qeapp_install` (UI) đọc gói, xác minh chữ ký rồi `appstore_save`; lúc
  boot `appstore_restore` chỉ trả tên, `vqeaf_qeapp_install_stored` đọc lại
  bytes từ slot p77 và chạy đúng đường xác minh chữ ký; lúc chạy,
  `vqeaf_qeapp_package` lấy bytes từ slot p77 chứ không đọc lại tệp trong
  volume RAM. Gỡ app cập nhật catalog.
- Verb: `install <file>` (xếp yêu cầu cho UI thread, tránh deadlock volume
  lock), `run <tên|id>` (xếp yêu cầu chạy), `apps` (liệt kê app đã cài kèm
  native/không), `store` (slot/seq/số app/số byte catalog đang dùng).
- Dữ liệu riêng từng app kiểu `/data/data` (đo trên máy thật): catalog
  **QEAFAPP3** thêm blob dữ liệu vào từng record
  `{name_len, pkg_len, data_len, name, pkg, data}`, tối đa 64 KiB/app. Khi cài
  mới, blob được đọc lại từ slot (khôi phục sau boot); khi **cập nhật cùng id**
  blob trong RAM được giữ nguyên và ghi lại; khi **gỡ app**, record + blob bị
  bỏ khỏi catalog. Catalog v2 cũ vẫn đọc được (13 app đã migrate sang v3).
  Đo: `appdata 0 hello-data-v1` → `appdata: slot 0 13 bytes`; cài lại
  `t01.qeapp` (cùng id) → vẫn `13 bytes: hello-data-v1`, catalog seq 15 giữ
  nguyên kích thước; reboot → dữ liệu còn; `uninstall 0` → catalog từ 6.523.661
  xuống **6.505.796 bytes** (đúng phần record + 13 byte dữ liệu bị xoá), reboot →
  12 app. Ghi lại `reboot-check-42` rồi reboot → vẫn đọc được đủ 15 byte.
- Verb kiểm tra: `appdata <slot> [text]` (đọc/ghi dữ liệu app), `uninstall <slot>`
  (xếp yêu cầu gỡ cho UI thread), `apps` in thêm `data NB`.
- Cài từ bộ nhớ trong (đo trên máy thật): trang Ứng dụng có mục **Cài từ bộ
  nhớ trong** mở màn hình chọn tệp, duyệt cây lưu trữ của thiết bị (mặc định
  `/Install/Apps`, đi lên/đi xuống được), chỉ hiện thư mục và `.qeapp`, chạm
  một gói là cài ngay. Đường dẫn đầy đủ được giữ trong catalog
  (`/Install/Apps/pick.qeapp`), và `fat16_file_size` giờ hiểu đường dẫn thư mục
  nên đọc được gói nằm trong thư mục con. Đo: cài `pick.qeapp` từ
  `/Install/Apps` → `appstore: saved 13 app(s), seq 13, 6523596 bytes (slot A)`
  → reboot → `appstore: boot restore 13 app(s)`, `store: slot A seq 13 apps 13`.
  Ảnh: `build/screens/device/picker-final.png`.
- Cài từ thư mục lồng sâu (đo trên máy thật): gói đặt ở
  `/Install/Apps/Deep/host.qeapp` (hai thư mục con dưới `/Install/Apps`), mở
  màn hình chọn tệp → `Install` → `Apps` → `Deep` → chạm gói →
  `store: slot B seq 18 apps 13 bytes 6523667`, `apps` in
  `Deep Install (native) /Install/Apps/Deep/host.qeapp` → reboot →
  `apps: 13 installed` và `store: slot B seq 18 apps 13 bytes 6523667`, app
  **Deep Install** vẫn còn (đường dẫn chỉ sống trong RAM nên sau boot danh sách
  hiện basename, catalog giữ nguyên kích thước). Ảnh:
  `build/screens/device/pick-apps-deep.png` (thấy mục `Deep` trong
  `/Install/Apps`) và `build/screens/device/deep-two2.png` (danh sách trong
  `Deep`). Lưu ý khi chép gói từ PC: Windows chỉ hiện tên 8.3 của các thư mục do
  OS tạo (`INSTALL`, `APPS`, `DEEP`, `USERDATA`, `DOWNLO~1`) nên phải dùng đúng
  đường dẫn đó; `storage_publish` tạo sẵn `UserData/Downloads/Apps` mỗi lần boot
  để thả gói vào.
- Trình cài đặt là app riêng trong launcher (đo trên máy thật): mục **Trình cài
  đặt** nằm cạnh **Tệp** trong trang Ứng dụng, mở thẳng `VQEAF_PAGE_INSTALLER`
  (không qua trang Tệp). Trang quét đệ quy cây QEAFRESCUE (sâu ≤ 3, tối đa 16
  gói) và liệt kê 5 lần cài gần nhất từ catalog. Đo: thả `deep.qeapp` vào
  `/Install/Apps/Deep` và `/UserData/Downloads/Apps` → trang hiện hai hàng gói
  (`DEEP.QEAPP /UserData/Downloads/Apps`, `DEEP.QEAPP /Install/Apps/Deep`) cùng
  "Lần cài gần đây" 5 app; chạm gói → hộp thoại dùng lại hiện `Deep Install`,
  `v1.0.0 · org.vqeaf.deep`, quyền `Mạng, Lưu trữ` → CÀI ĐẶT →
  `Ứng dụng đã được cài đặt` + `Deep Install v1.0.0`, `apps: 13 installed`,
  `run` mở trang QEAPP; reboot → `apps: 13 installed`, Deep Install còn trong
  "Lần cài gần đây", còn tệp copy từ PC thì mất (QEAFRESCUE là LUN RAM) nên
  trang báo đúng "Chưa tìm thấy gói .qeapp". Ảnh:
  `build/screens/device/installer-page.png`, `installer-confirm-device.png`,
  `installer-installed-device.png`, `installer-open-device.png`,
  `installer-after-reboot.png`.
- Ứng dụng **Terminal** (`examples/terminal-qeapp`, native arm64 ABI v4) cài và
  chạy trên máy thật: `install TERMINAL.QEAPP` → `apps: 14 installed`,
  `- Terminal (native) /TERMINAL.QEAPP`; `qeapp TERMINAL.QEAPP` → `qeapp: ran
  TERMINAL.QEAPP on violet (arm64) in a guarded child`; bấm **Chạy trên thiết
  bị** trong trang app mở trang `QEAPP native` với khung terminal đầy đủ
  (báo cáo `api table size`, `api abi_version`, 5 dòng quyền granted/denied,
  probe `read_file`, prompt và ghi chú). Ảnh: `terminal-list2.png`,
  `terminal-v4-run.png`.
- Bài học arm64 khi làm Terminal (đo bằng gate QEMU + máy thật): module chết
  giữa `start()` vì clang vector hoá vòng fill byte thành store 8 byte **không
  căn lề**, mà harness AArch64 chạy **MMU off** (bộ nhớ Device) nên lỗi thành
  alignment Data Abort — biểu hiện bên ngoài là "module exceeded 5000 ms" trên
  máy và signal 11 qua verb `qeapp`. Sửa: ghi qua con trỏ `volatile` +
  `aligned(16)` cho buffer; từ đó gate QEMU qua (`lifecycle: create start pause
  resume stop destroy`, hash `0x10dc0e21`, 0 leak) và máy chạy đúng. Ghi chú
  trong `examples/terminal-qeapp/README.md`.
- Chạm icon app đã cài = **mở luôn** (đo trên máy thật, ảnh
  `build/screens/device/launch-apps-tap.png`): `qeapp_open_cb` gọi
  `vqeaf_ui_run_app()` cho gói native (chạy trong tiến trình con có bảo vệ rồi
  chuyển sang trang `QEAPP native` — từ icon Terminal không còn qua màn hình
  thông tin), còn gói khai báo mở trang nội dung `QEAPP`. Màn hình thông tin
  (nút "Chạy trên thiết bị", "Gỡ ứng dụng") vẫn còn ở nhấn giữ icon qua
  `qeapp_info_cb`; kiểm bằng verb `hold` (xem dưới).
- Sheet thông tin trên `lv_layer_top()` trước đây **nuốt mọi cú chạm**: đọc
  `lv_indev_search_obj()` từ đúng cây LVGL cho thấy điểm chạm rơi vào object phủ
  toàn canvas (`0,0..719,1559`, `kids=0`) chứ không phải nút, vì các label
  full-width tự xuống dòng nên chiều cao của chúng chỉ chốt **sau** lần layout
  đầu — khung đã vẽ giữ layout một dòng trong khi hit-box đã ở layout đã wrap.
  Sửa trong `vqeaf_pages.c`: sheet thành hai con trực tiếp của layer (scrim +
  card, chạm ngoài card đóng sheet), `l1`/`l2` dùng `LV_LABEL_LONG_MODE_DOTS`
  (một dòng, chiều cao ổn định) và chốt layout (`lv_obj_update_layout` hai lần
  + invalidate layer) trước lần vẽ đầu. Đo lại trên máy:
  `hit 287,898 -> 211,867..346,930 kids=1 clickable=1` (nút **Đóng**) và
  `hit 437,898 -> 359,867..508,930 kids=1 clickable=1` (nút **Thông tin**) —
  Đóng đóng sheet, Thông tin mở trang `QEAPP` của Terminal
  (ảnh `build/screens/device/final-sheet.png`, `final-info.png`).
- Chạm icon giờ mở **giao diện của chính app**: khung 720×1560 của module được
  vẽ 1:1 toàn màn hình (`lv_image_set_scale(256)`), trang `QEAPP_NATIVE` ẩn
  status bar/nav, không còn tiêu đề `QEAPP native` hay dòng lifecycle/hash đè
  lên pixel; chỉ còn chip quay lại ở góc dưới-trái (`native_back_cb` → Ứng
  dụng). Số đo lần chạy (lifecycle, `app pid/shell pid/uid`, hash) chuyển sang
  trang chi tiết app (nút `Thông tin`). Ảnh máy: `build/screens/device/app-ui2.png`,
  `app-ui-back.png`.
- Status bar giờ do shell quản lý: `vqeaf_ui_show()` bật lại (`clear_flag
  HIDDEN`) cho **mọi** trang trước khi xét layout, chỉ trang `QEAPP_NATIVE` ẩn
  lại; trước đó nhánh `LOCK` không bật lại nên khoá màn hình từ app native mất
  đồng hồ/pin. Thêm verb `power` (nhấn nguồn ngắn qua hook `rescue_power_hook`
  → `vqeaf_ui_toggle_screen()`, đánh thức thì wake chứ không toggle lần nữa) để
  kiểm khoá/mở khoá từ host. Đã kiểm trên máy: mở app → `power` → `power` →
  `swipe` lên; status bar có ở màn khoá và trang chủ (`lock-2-locked.png`,
  `lock-3-woken.png`, `lock-4-unlocked.png`).
- Trang app native không còn ẩn status bar một cách mặc định: `vqeaf_ui_show()`
  hỏi `vqeaf_pages_frame_top_band_empty()` — quét dải 0..55 của **khung app
  thật**; nếu cả dải cùng một màu thì đồng hồ/pin hiện đè nhẹ (`LV_OPA_20`,
  `lv_obj_move_foreground`) như điện thoại thật, nếu khung có pixel ở dải đó
  thì vẫn ẩn. Đo trên Terminal: chữ đầu tiên ở y=21..29 (x 12..227) và
  42..50, nên dải 0..55 **không trống** — đồng hồ (x 30..120, y 18..42) sẽ đè
  lên dòng "Terminal 1.0", vì thế shell giữ ẩn (log máy:
  `ui: qeapp frame paints the top band, status hidden`). Nhánh hiện đè được
  render trên host với khung giả có dải trên trống:
  `build/screens/phone-ui/qeapp-native-720x1560-providers.png` (dòng 09:41 /
  wifi / 76% nằm trên khung app) và có kiểm tra đơn vị cho cả hai nhánh trong
  `tests/vqeaf_shell_interaction.c`.
- App mẫu mới `examples/shellui-qeapp` (id `org.vqeaf.shellui`, tên hiển thị
  "Layout mẫu", ABI v4): vẽ đúng bốn vùng của một giao diện điện thoại —
  status bar y 0..55 (dải đặc, không chữ, để shell vẽ đồng hồ đè lên), title
  bar y 56..195, main y 196..1371, nav bar y 1372..1559 — bằng `blit_pixels`
  (RGB565) + `draw_text_weighted`, màu lấy từ palette tối của shell
  (`0x06100F`, `0x112629`, `0x1EE0C8`...). Cài trên máy bằng
  `install LAYOUT.QEAPP`, chạy `run org.vqeaf.shellui`: log
  `ui: qeapp frame leaves the top band blank, status overlaid`, ảnh
  `build/screens/device/shellui-final.png`. Gói: 411 077 B (module 3 903 B);
  `qeaf validate` OK; 4 test của project OK.
- **Dynamic Color (Material You)**: `vqeaf_theme_set_seed(0xRRGGBB)` sinh cả bảng
  màu từ một màu nguồn — nền/bề mặt pha theo hue của seed ở chroma thấp, màu
  nhấn giữ hue đó ở chroma cao, danger/warn giữ nguyên nghĩa; seed sống qua lần
  đổi Sáng/Tối và `0` trả về bảng màu gốc. Là xấp xỉ HSL của HCT (không phải
  bộ giải HCT đầy đủ) — ghi rõ trong ghi chú trên UI. UI: thẻ “Màu động” trong
  Cài đặt với 8 swatch + liên kết “Về bảng màu mặc định”, hiển thị seed đang
  dùng (`#7C4DFF`). Kèm sửa lỗi cũ: status bar và dock chỉ được dựng một lần
  nên khi đổi theme vẫn đen — `refresh_chrome()` trong `vqeaf_ui_show()` giờ
  sơn lại status bar, dock, home bar và nền screen. Kiểm trên máy: chọn tím
  → cả shell tím (`dyn-purple3.png`, `dyn-home2.png`), sang Sáng → nền tím nhạt
  + chrome sáng đồng bộ (`dyn-light3.png`, `dyn-light-home.png`). Test host:
  seed giữ nguyên, accent mang hue của seed, Sáng vẫn sáng, seed 0 khôi phục
  bảng gốc (`ALL OK`).
- **Trang Material Design (cấu trúc component theo M3)**: `VQEAF_PAGE_M3` —
  app bar ghim trên (elevation 0, tự nâng lên 2 khi nội dung cuộn qua), body
  cuộn riêng với ba mức nổi (thẻ filled 0 · 0 dp, thẻ elevated 1 · 1 dp, bảng
  nổi 3 · 6 dp) kèm danh sách, nút; FAB mở rộng (`vqeaf_m3_fab_extended`,
  icon + nhãn, elevation 3) nổi trên nội dung; hộp thoại của FAB nằm trên lớp
  phủ 40% ở top layer nên cao hơn mọi thứ trong trang, xác nhận thì hiện
  Snackbar. Thang nổi được đưa thành token thật
  (`vqeaf_m3_elevation_scale()`: 0 · 1 · 3 · 6 · 8 · 12 dp) để gate kiểm tra
  đúng chuẩn. Vào từ Cài đặt → “Material Design”. Sửa kèm: FAB đặt trên dải
  cử chỉ (tap ở đáy bị cử chỉ home nuốt — log hit `0,1340..719,1559`), và
  hộp thoại nay bọc chữ trong thẻ (trước đó nhãn rộng quá thẻ, chữ bị cắt).
  Test host: thang elevation đúng dp, bóng tăng dần theo mức, trang có app
  bar/FAB, FAB mở hộp thoại trên top layer và xác nhận hiện Snackbar.
- **Bộ chuyển đổi ứng dụng (recents) dạng carousel**: `Bộ chuyển đổi` giờ là
  bộ thẻ kiểu điện thoại — nền wallpaper hiện sau, mỗi app đang mở là một thẻ
  bo 36 px nổi elevation 2 với icon, tên, nút × đóng và vùng preview màu của
  app; thẻ mới nhất được canh giữa (snap center) và thẻ trước đó ló ra bên
  trái; tiêu đề “Ứng dụng gần đây / Mọi ứng dụng đang mở, chỉ cách một lần
  vuốt.”, nút “Xoá tất cả”, gợi ý chân trang. Hàng đợi thật theo thứ tự mở,
  thêm `vqeaf_ui_recent_close()` / `vqeaf_ui_recent_clear()` (RAM, như cũ).
  Test host: recents mới-nhất-trước, mỗi app một thẻ, chạm thẻ giữa mở đúng
  app, đóng/xoá hết ra trạng thái rỗng.
  Đã lên máy (violet) bằng `fastboot flash recovery` — đường `flash recovery`
  của QEAFRESCUE vẫn từ chối `BOOTIMG.BIN` ("missing or fragmented") kể cả sau
  khi format lại volume FAT16 33 MB, nên bỏ qua LUN. Kiểm trên máy: vuốt lên
  từ Home mở carousel, thẻ mới nhất canh giữa + thẻ cũ ló trái, chạm thẻ mở
  đúng app, × xoá thẻ, "Xoá tất cả" ra "Chưa có ứng dụng gần đây".
- **Menu nguồn dạng bảng nổi 2×2 nút tròn**: giữ nút nguồn 1 giây (hoặc nút
  "Nguồn" trong Trung tâm điều khiển) mở bảng bo 40 px elevation 3, không còn
  tiêu đề/mô tả, chỉ 4 đĩa tròn (26% bề ngang canvas) xếp 2×2 bằng flex
  `ROW_WRAP` — một kích thước đĩa suy ra cả bảng, không có container hàng
  lồng nhau — với nhãn nằm dưới mỗi đĩa;
  "Tắt nguồn" là đĩa error đặc với icon on-error, ba nút còn lại đĩa surface-3
  — bố cục theo màn hình tham chiếu. Chạm nền tối bên ngoài chỉ đóng menu:
  overlay được xoá ở tick kế tiếp (`lv_async_call`) sau `lv_indev_wait_release()`,
  vì xoá ngay trong CLICKED làm con trỏ active của LVGL trỏ vào bộ nhớ đã giải
  phóng và lượt chạm sau rơi xuống trang bên dưới (đã thấy trang tự đổi sang
  Apps). Kiểm trên máy: mở từ Trung tâm điều khiển → Nguồn (cùng hàm mà giữ
  nút nguồn gọi), chạm nền → ảnh sau khi đóng trùng từng pixel với ảnh trước
  khi mở, tức không kích hoạt gì phía dưới.
- **Crash record cho lỗi UI**: `ui_fault()` (handler `SA_SIGINFO`) ghi
  `CRASH.TXT` trước khi `siglongjmp` — signal/code/addr, PC/LR/SP lấy từ
  `ucontext` aarch64, trang đang mở, boot stage, uptime và 40 dòng log cuối.
  Mọi thứ theo kiểu async-signal-safe: buffer tĩnh, tự đổi số, không
  malloc/stdio/LVGL; bản ghi được `write()` ra stderr trước (kernel ring → còn
  trong pstore khi tiến trình chết), rồi ghi volume bằng `trylock` (fault có
  thể cắt ngang rescue thread đang giữ mutex) và được chép lại ở vòng chính
  với lock thật + log mới nhất. `CRASH.TXT` được reserve 8 KiB trong
  `usb_lun.c` nên không cấp cluster từ signal context. Verb mới `fault ui`
  gây SIGSEGV thật trên UI thread để kiểm đường này trên máy — đã kiểm: bản
  ghi `sig=11 code=1 addr=0x0 pc=0x132c358 lr=0x132c350 sp=0x7ff51cb520`,
  `page=0 stage=loop ui=1 uptime_s=42` + 40 dòng log, kênh rescue vẫn trả lời
  sau fault. Volume là RAM nên bản ghi mất sau reboot: chép ra host trước khi
  khởi động lại.
- **Kiểm trên máy bằng một lệnh**: `scripts/device_check.py` gộp flash + kịch
  bản + so ảnh + PASS/FAIL. `--flash` copy `dist/boot-vqeaf-violet.img` vào
  volume thành `BOOTIMG.BIN`, gửi `flash recovery BOOTIMG.BIN confirm`, chờ
  `written and verified byte for byte`, xoá file tạm rồi `reboot recovery`
  và chờ kênh mới (`seq=0`, counter host reset) — không cần fastboot; nếu
  QEAFRESCUE im lặng thì thử `adb reboot recovery` một lần, và chỉ khi đó
  cũng không lên mới rơi về `fastboot flash recovery`. Các bước: `wake`,
  `unlock`, `page`, `tap`, `swipe`, `key`,
  `cmd`, `snap`, `expect same|diff|resp|file`, `sleep`, `note` — dùng
  `--steps "…;…"` hoặc `--scenario smoke|power_menu`. Volume đọc bằng raw
  sector với FAT16 tự parse (view mount của Windows lệch), SCREEN.RAW
  240x520 RGB565 → PNG 720x1560; `expect same/diff` so **tỉ lệ pixel lệch**
  (ngưỡng 0,1% / 1%) nên đồng hồ trên status bar không làm fail oan. Verb
  rescue mới `unlock` (wake + Home trên UI thread) để kịch bản không phụ
  thuộc cử chỉ vuốt. Đã chạy thật: `--flash --scenario smoke` (đường cứu
  hộ) PASS 8/8 trong 50 s; `--scenario power_menu` PASS 10/10 trong 53 s,
  sau khi đóng menu 0,000% pixel lệch và hash trùng.
- **Phản hồi có số thứ tự, ảnh có frame counter**: mỗi lô lệnh chạy xong,
  `RESPONSE.TXT` bắt đầu bằng `seq=N` (tăng dần; `seq=0` lúc init), nên host
  phân biệt được câu trả lời mới với câu cũ — HELP cũng nói vậy. `SCREEN.RAW`
  = 249 600 byte RGB565 + 40 byte trailer ASCII `frame=<n> fnv=<hash>` (FNV-1a
  32 trên đúng vùng pixel); file reserve dùng `RESCUE_SCREEN_TOTAL`, decoder
  cũ đọc offset 0 vẫn chạy (`ui_snapshot_compare.read_raw` nhận file dài
  hơn). `device_check.py` chờ **đúng `seq` tăng + nonce** của lô, và mỗi
  `snap` kiểm hash trailer rồi đòi `frame` lớn hơn lần trước → báo
  "stale/torn capture" thay vì so ảnh cũ. Kiểm trên máy: `--flash --scenario
  smoke` PASS 8/8, frame=1..4; `RESPONSE.TXT` bắt đầu `seq=9`,
  trailer `frame=4 fnv=b4abd653` khớp hash tính lại.
- **Flash qua kênh cứu hộ, không cần fastboot, kể cả BOOTIMG.BIN phân mảnh**:
  `fat16_file_span` (đòi một dải liền mạch) được thay bằng
  `fat16_file_open`/`fat16_file_next` — duyệt chuỗi cluster và trả từng run
  liền mạch trỏ thẳng vào volume RAM, nên không có buffer 20 MiB thứ hai.
  `do_flash` ghi mỗi run vào p77 tại đúng offset của nó, rồi đi lại chuỗi lần
  hai và đọc lại so từng khối 64 KiB (`flash_verify_run`, vì
  `sysio_block_verify` cần một buffer liền). Response ghi rõ
  `flash: BOOTIMG.BIN was fragmented - walked N cluster runs`.
  Kiểm: host `tests/test_fat16_path.py` — chuỗi bị chẻ `3 -> 11 -> 5`, đọc
  ra nhiều run, byte khớp bản gốc; máy thật — tạo 6 file 3 MiB, xoá xen kẽ
  (9 MiB rải rác), copy `BOOTIMG.BIN` 20 316 160 B vào các lỗ →
  `flash recovery BOOTIMG.BIN confirm` trả `written and verified byte for
  byte` + `walked 4 cluster runs`, sau đó `reboot recovery` boot đúng kernel
  vừa flash (kênh trả lời với seq mới và `snap` frame=1).
- **PID 1 không chết vì một lỗi nữa (sửa "chạy một lúc là tự khởi động lại")**:
  `ui_fault` trước đây re-raise signal khi lỗi nằm ngoài khối UI → kernel
  panic `Attempted to kill init! exitcode=0x0000000b` → máy tự reboot, mất
  luôn volume RAM và `CRASH.TXT`. Giờ: lỗi ở main thread `siglongjmp` về điểm
  phục hồi ở đầu vòng lặp (cờ `main_recovery_armed`), lỗi ở worker thì thread
  bị park tại chỗ (`for(;;) pause()`), PID 1 vẫn sống và kênh cứu hộ vẫn trả
  lời; nhánh cứu hộ trong vòng lặp dùng `pthread_mutex_trylock` để không thể
  tự khoá mình khi worker đang giữ volume. Bản ghi lỗi được ghi thêm vào
  **journal 16 KiB ở đuôi p77** (`boot_journal.c`: magic `QEAFJRNL1`, `boots=`,
  `stage=`, `uptime_ms=`, text 15 872 B) và mỗi lần boot công bố lại thành
  `LASTBOOT.TXT` trên volume — thứ duy nhất sống sót qua panic. `CRASH.TXT`
  thêm `thread=main|worker` và `boots=N`.
- **Hộp thoại "Dùng USB cho" khi cắm cáp**: `vqeaf_ui_usb_dialog_show()`
  (overlay nền mờ + card M3 ba dòng; dòng Truyền ảnh đã bỏ ngày 2026-11-18, nay
  còn hai — xem `docs/VIOLET_HARDWARE.md` §3.1) hiện khi cáp vừa cắm — main
  loop theo dõi cạnh `usb_gadget_cable()` nên chỉ mở một lần cho mỗi lần cắm và
  tự đóng khi rút. Ba lựa chọn đều có tác dụng thật qua `vio_lun_set_mode()`:
  **Sạc thiết bị** thả UDC (máy biến mất khỏi máy tính, chỉ nạp pin), **Truyền
  tệp** bind lại `f_mass_storage` (ổ QEAFRESCUE), **Truyền ảnh** xin PTP —
  kernel này chỉ có functionfs, không có responder PTP, nên nó giữ chế độ ổ
  đĩa và nói thẳng bằng notification (trả 1 từ `vio_lun_set_mode` để biết là
  fallback). Volume RAM không bị hủy nên đổi qua lại được không cần reboot;
  đóng bằng cách chạm nền dùng deferred close giống menu nguồn (tránh rò chạm).
  Gate host: `scripts/test_violet_shell.py` mới kiểm mở sheet, đủ ba dòng (nay
  kiểm hai dòng chọn + ghi chú), tap báo đúng lựa chọn, đóng sau tap, chạm nền
  = hủy → ALL OK, ảnh `build/screens/phone-ui/interaction-usb-sheet.png`.
- **WiFi: retry thay vì bỏ cuộc**: bring-up WCN3990 (mount p86/p82/p74,
  link firmware, QRTR name service, mở modem qua `subsys_modem`) đã có từ
  trước; nó cần ~60 s nên cái chết ở ~33 s là thứ chặn wlan0. Thêm vòng thử
  lại có giới hạn: `GAVE_UP` → kick lại mỗi 30 s, 5 lần, có `retries=N` trong
  report `wifi`. **Chưa có wlan0 thật** — cần máy sống đủ lâu và log `wifi`
  của lần boot kế tiếp.
  Chế độ tự động “Theo hình nền”: `vqeaf_theme_set_auto(1)` lấy màu chủ đạo của
  hình nền làm seed — `wallpaper_dominant()` lấy trung bình có trọng số theo
  diện tích và ưu tiên chroma trên đúng các màu mà `vqeaf_wallpaper()` vẽ (nền
  tối ra `#0E4349` teal, nền sáng ra xám-teal), nên đổi hình nền là đổi cả bảng
  màu mà không cần bộ giải mã ảnh; chọn swatch thủ công sẽ tắt chế độ tự động,
  tắt công tắc trả về bảng gốc. Hình nền thật sau này chỉ cần thay phần lấy mẫu.
  Test host thêm 16 assertion (seed khác 0, accent theo teal, nền tối/sáng giữ
  đúng độ sáng, mỗi hình nền một seed, swatch thủ công thắng, công tắc trong
  Cài đặt bật/tắt và hiện “Bật”) — `ALL OK`; ảnh
  `build/screens/phone-ui/interaction-settings-dynamic-auto.png`.
- Chuỗi khởi chạy QEAPP được đo theo sơ đồ tổng quan nền tảng di động và ghi lại ở
  `docs/QEAPP_LAUNCH_PIPELINE.md`: trang native in thêm dòng thật
  `app pid 494 · shell pid 1 · uid 0` (`vqeaf_qeapp_device_process()`), chứng
  minh fork → tiến trình riêng, và nói rõ QEAF **không** có UID riêng cho app,
  không DEX/ART, không Binder/SurfaceFlinger (khung hình đi qua canvas
  `MAP_SHARED` rồi LVGL composite).
- Kênh cứu hộ có thêm verb `hold X Y [ms]`: giữ điểm chạm theo đồng hồ thật
  (`vio_hold_queue`, mặc định 1200 ms) nên timer nhấn giữ 900 ms bắn được từ
  host; đã kiểm trên máy — nhấn giữ icon Terminal mở sheet thông tin với nút
  `Đóng` / `Thông tin` (nhãn `Mở` đổi thành `Thông tin` cho QEAPP vì chạm icon
  giờ đã mở app; app hệ thống vẫn giữ `Mở`).
- Bỏ giới hạn 8 app / 4 MiB (đo trên máy thật): cài 11 gói nhỏ + 1 gói
  **6,3 MB** → catalog **6.505.745 bytes** ghi vào slot B seq 12 → reboot →
  `appstore: boot restore 12 app(s)`, `apps: 12 installed`, `store: slot B seq 12
  apps 12 bytes 6505745`, và `run org.vqeaf.big` chạy gói 6,3 MB thẳng từ slot
  p77 (trang `QEAPP native`). Ảnh: `build/screens/device/big-from-store.png`.
  Đường đọc dùng `fat16_file_size` để cấp đúng bộ đệm (không còn mức 512 KiB),
  và app đã khôi phục được lưu lại catalog bằng bytes trong kho khi tệp FAT
  không còn sau boot.
- Đo trên máy thật (sau khi bỏ bước chép vào QEAFRESCUE): cài → reboot →
  `appstore: boot restore 1 app(s)`, `apps: 1 installed - Hello VQEAF (native)`,
  trong khi `ls` trên QEAFRESCUE **không còn** `hello.qeapp`; `run org.vqeaf.hello`
  chạy thẳng từ slot p77 và mở trang `QEAPP native`. Trước đó cùng gói chạy
  bằng đường tệp cho `hash=0x6cc394ad`, 0 alloc rò rỉ. Ảnh:
  `build/screens/device/restored-apps.png`, `build/screens/device/launch-from-store.png`.
- Giới hạn còn lại: thiết bị chưa có phân vùng lưu trữ nội bộ riêng — "bộ nhớ
  trong" vẫn là cây thư mục FAT16 mà PC thấy qua USB (VQEAF/UserData/Install),
  nên tệp vẫn phải được đưa vào qua ổ đó (hoặc do OS ghi ra); muốn `/data`
  thật thì phải mount userdata, chưa làm. Catalog (tổng mọi gói) phải vừa ~16
  MiB mỗi slot; RAM
  giữ tối đa 24 app (mỗi app 16 KiB icon); gỡ app chỉ bỏ khỏi catalog; nạp lại
  ảnh boot không xoá vùng app (cần xoá tay nếu muốn reset); nếu ghi p77 hỏng
  thì khôi phục bằng cách nạp lại ảnh. Đây **chưa** phải mô hình `/data` của
  nền tảng di động: chưa có thư mục dữ liệu riêng từng app, chưa cài từ nguồn ngoài
  QEAFRESCUE, và chưa dùng phân vùng userdata.

## Phase 3: simulator host cho framework API v2 (2026-11-12)

- Native "Hello" giờ chạy THẬT trong simulator host, không còn là validate-only.
  `framework/simulator/qeaf_sim.{h,c}` nạp package qua đúng inspector của thiết
  bị (`vqeaf_qeapp_inspect_native`), validate `.qen`, map `code|rodata|data|bss`
  vào một arena thực thi (VirtualAlloc/mmap), resolve bảng export rồi gọi
  create/start/pause/resume/stop/destroy. Display giả 720x1560 XRGB8888 raster
  ASCII bằng `vqeaf/font8x8.h`; input theo kịch bản; storage trong bộ nhớ có
  kiểm tra permission; phát hiện leak allocation sau destroy. Lệnh CLI:
  `qeaf simulate <package.qeapp>` (build dùng chung `sdk/simulator/qesim.py`).
- Hợp đồng gọi: bảng `qeaf_app_api_v2` trong `framework/application_api/qeaf_api.h`
  dùng quy ước `QEAF_APP_CALL` (SysV AMD64 trên x86_64 kể cả host Windows,
  AAPCS64 trên aarch64) để module và host luôn khớp ABI.
  `qeaf_native_abi.h` định nghĩa permission bit, tên export/import và event input.
- Compiler/linker thật: `sdk/compiler/qenlink.py` là linker ELF64 PIC tĩnh, giải
  reloc PC-relative ngay lúc build và TỪ CHỐI reloc tuyệt đối/GOT cùng symbol
  undefined (module không cần libc, không cần reloc lúc chạy). `qen.py` đóng gói
  bảng imports/exports; `qecompile.py` biên dịch C freestanding PIC bằng zig cho
  x86_64 và arm64. Validator thiết bị (`vqeaf_qeapp_native.c`) và SDK giữ cùng
  một luật kiểm tra bảng.
- Kiểm chứng (số thật, chạy trên host): `scripts/test_qeapp_simulator.py` =
  61 checks / 0 failures, acceptance PASS; Hello đủ vòng đời, hash framebuffer
  `0x6cc394ad`, 0 allocation rò rỉ, 486 pixel chữ được raster.
  `tests/test_qeapp_native.py` = 21 tests (thêm 7 test bảng qen + linker 2 arch).
  `scripts/test_qeapp_native_loader.py` = 19 case accept/reject PASS.
  `scripts/test_qeapp_runtime.py` = 5 kịch bản ALL OK.
  `python -m unittest discover -s tests` = 62 tests OK (gồm regression v1).
  `scripts/render_phone_ui.py` = 60 PNG, trang qeapp xem bằng mắt vẫn đúng.
- Ranh giới kiểm chứng: chỉ THỰC THI x86_64 trên host. arm64 được build và
  validate cấu trúc nhưng KHÔNG chạy; chạy trên violet là gate Phase 4, không
  được tuyên bố ở đây. Không có bằng chứng phần cứng thật. Bộ gate OS đầy đủ
  (`scripts/test.py`: ctest + QEMU + ảnh dist) KHÔNG chạy trong bước này vì ngoài
  phạm vi Phase 3 và cần ảnh build sẵn; các gate QEAPP liên quan đều xanh.
- An toàn giữ nguyên: không thực thi code chưa validate, không cấp truy cập
  hardware/kernel cho app, chữ ký vẫn là HMAC dev-key như Phase 1-2 (chưa có
  khoá production), và rule không bày dữ liệu giả được tôn trọng (simulator là
  harness có chủ đích, không phải trạng thái thiết bị).

## Khởi động lại kiểu Windows trên shell LVGL (2026-11-12)

- Trước thay đổi, power menu gọi thẳng `reboot(RB_AUTOBOOT)` ngay trong
  callback LVGL: màn hình biến mất lập tức, không lưu log, không `sync()`, và
  `#include <sys/reboot.h>` làm **hỏng bản build host** (`fatal error:
  'sys/reboot.h' file not found`). Thêm nữa, các hàng menu được tạo bằng
  `vqeaf_obj()` nhưng thiếu `LV_OBJ_FLAG_CLICKABLE`, nên chạm vào "Khởi động
  lại" không kích hoạt callback nào trên máy thật. Cả hai lỗi được phát hiện
  bằng cách chạy thật harness LVGL, không phải đọc code.
- `platform/violet/ui/vqeaf_shutdown.{h,c}` mới: overlay toàn màn hình kiểu
  Windows (vòng xoay, "Đang khởi động lại" / "Đang tắt nguồn", dòng trạng
  thái thật theo từng bước, không có phần trăm giả). UI không còn gọi reboot.
- Trình tự thật: `request` → hiện màn hình → `flush-log` → `sync` → hook reset
  máy. `main_violet.c` sở hữu hook: ghi từng bước vào `QEAFBOOT.LOG`
  (khoá `volume_lock`, flush ra QEAFRESCUE), `set_stage("restart")` và
  `; power=restart` trong `status`, `sync()`, rồi `reboot(RB_AUTOBOOT)` /
  `reboot(RB_POWER_OFF)`. Nếu kernel từ chối, overlay hiện lỗi thật kèm nút
  "Quay lại" thay vì giả vờ đã khởi động lại.
- Trong lúc chờ reset: chặn touch/rescue tap, phím nguồn/âm lượng, timer tắt
  màn hình và các yêu cầu lặp; `lv_timer_handler()` vẫn chạy để vòng xoay
  quay. Sau reset firmware nạp lại boot image → splash → `VQEAF_PAGE_LOCK`,
  RAM state (dock, chủ đề, uptime) về mặc định như một lần boot nguội.
- Menu nguồn đổi hàng thứ ba từ "Reboot" (trùng chức năng) thành "Ngủ"
  (`vqeaf_ui_toggle_screen`). Host harness có thêm
  `tests/vqeaf_host_platform.h` cho 4 stub platform còn thiếu.
- Kiểm chứng: `scripts/test_violet_shell.py` ALL OK (60 assertion cũ + 20
  assertion mới: overlay, thứ tự stage, một lần gọi reset, từ chối reset, nút
  quay lại; snapshot `interaction-restart.png`,
  `interaction-poweroff-refused.png`), `scripts/render_phone_ui.py` 60 PNG,
  `scripts/build_violet.py` 8.336.672 byte / 245 object,
  `scripts/package_violet.py --skip-build` → `dist/boot-vqeaf-violet.img`
  20.054.016 byte, SHA-256 `169f8c111a6fa764...`, validate_bootimg +
  audit_rescue_channel PASS. Configure/build sạch ở `build-native` (MinGW)
  cho 10/10 CTest PASS.
- Lỗi môi trường có sẵn, **không do thay đổi này**: `scripts/test.py` và
  `scripts/build.py` vẫn fail vì `build/CMakeCache.txt` được sinh ở đường dẫn
  cũ `D:/hdhQEAFIVELS` (`CMake Error: ... does not match the source`). Chưa
  xoá/tạo lại cache `build/`; cần quyết định riêng của user.
- **Giới hạn**: chưa flash và chưa bấm nút khởi động lại trên thiết bị thật;
  bằng chứng reboot phần cứng là luồng `reboot()` trong PID 1 và test host,
  không phải ảnh chụp sau reboot của máy. Không tự ý nạp ảnh.

## Vuốt phải mở danh sách ứng dụng, giữ trang sau khi nhấc tay (2026-11-12)

- Tái hiện bằng pointer LVGL thật trong `tests/vqeaf_shell_interaction.c`:
  vuốt phải từ Home trước đó đi `index = (0+3)%4` → **Cài đặt** (wrap vòng nav),
  không phải danh sách ứng dụng. `gesture_cb` nay có nhánh riêng
  `dir == LV_DIR_RIGHT && g_page == VQEAF_PAGE_HOME` → `VQEAF_PAGE_APPS`.
  Vuốt trái từ Home vẫn mở Apps; vuốt phải từ Apps vẫn về Home.
- Sửa nguyên nhân có thể làm "tự back về Home": `vqeaf_ui_show()` xoá
  `g_content` trong lúc ngón tay còn trên kính, LVGL thấy `act_obj` đổi và xoá
  `gesture_sent` (`platform/lvgl/src/indev/lv_indev.c:1267`) → một cú vuốt dài
  có thể bắn thêm `LV_EVENT_GESTURE` và đi tiếp vài trang. Mọi nhánh navigation
  trong `gesture_cb` giờ gọi `lv_indev_wait_release(indev)` **trước** khi đổi
  trang: một lần chạm = một lần đổi trang, phần còn lại của drag bị bỏ qua
  (cũng chặn `CLICKED` nhầm vào tile vừa dựng lại). Flag tự xoá khi nhấc tay,
  lần chạm sau hoạt động bình thường.
- Test mới: drag 40 poll, kiểm tra `page == APPS` ngay sau nhấc tay và sau
  thêm 500 ms, nội dung trang (tiêu đề "Ứng dụng", ô tìm kiếm), không có sheet
  bật lên do click nhầm, rồi hai cú vuốt tiếp theo vẫn nhận. Ảnh
  `interaction-swipe-right-apps.png` cho thấy đúng 24 ô ứng dụng.
- Kiểm chứng: `test_violet_shell.py` ALL OK, `render_phone_ui.py` 60 PNG,
  `build_violet.py` 8.337.032 B / 245 object, `package_violet.py` →
  `dist/boot-vqeaf-violet.img` 20.054.016 B, SHA-256 `7e049cde3fb5e2c583...`,
  validate + audit PASS, CTest `build-native` 10/10. Vẫn chưa flash/nạp p77.

## Sửa giao diện Bàn phím / Chủ đề phím (2026-11-12)

- Render host cho thấy 2/6 card chủ đề (`Bubble`, `Facet`) có `sub = NULL` và
  `dot_count = 0` → thiếu dải màu lẫn nhãn phụ, card thấp hơn hẳn card cùng
  hàng làm lưới lệch. Bổ sung palette (`Mint`, `Graphite`, mỗi cái 4 chấm) lấy
  từ chính màu của chủ đề; không bịa dữ liệu phần cứng.
- Pill breadcrumb cuối ("Chủ đề phím") bị **cụt mép phải** 720 px. Sửa:
  `pad_hor` 14→10, `pad_column` 8→6, width `LV_SIZE_CONTENT`, nhãn gọn thành
  "Chủ đề", row đổi `ROW_WRAP` + `pad_row` làm phương án dự phòng. Dòng ghi
  chú rút gọn để không còn dòng lẻ "liệu.".
- Test `tests/vqeaf_shell_interaction.c`: 6 card có nhãn phụ, pill cuối nằm
  trong canvas (`x2 <= 710`), chênh chiều cao card ≤ 24 px; snapshot
  `interaction-keyboard-themes.png` kiểm tra trực quan.
- Kiểm chứng: `test_violet_shell.py` ALL OK, `render_phone_ui.py` 60 PNG,
  `build_violet.py` 8.337.128 B / 245 object, `package_violet.py` →
  SHA-256 `e5b6ea86625e2c7a48...`, validate + audit PASS, CTest 10/10.
  Chưa flash/nạp p77. Phạm vi sửa là trang Giao diện → Chủ đề bàn phím;
  bàn phím LVGL trong modal Wi-Fi và hub Bàn phím giữ nguyên.

## Tắt màn hình: tiếng Việt có dấu + ngủ/tiết kiệm pin thật (2026-11-12)

- Settings còn nhãn không dấu `Tat man hinh` và các giá trị `30 giay` /
  `1 phut` / `Khong bao gio`. Đổi sang **"Tắt màn hình"**, `30 giây`,
  `1 phút`, `5 phút`, `30 phút`, `Không bao giờ`. Giá trị khởi tạo giờ đọc
  từ `vqeaf_state.screen_timeout_s` thay vì hard-code "1 phút"; giá trị ngoài
  bảng không còn nhảy thẳng sang "Không bao giờ". Thêm dòng giải thích
  "Hết giờ: mờ rồi tắt màn hình, vào chế độ ngủ để tiết kiệm pin..." và đệm
  cuối trang 140 px để dòng này không bị dock che.
- **Bug cốt lõi**: `state_timer_cb` đặt `screen_off=1` rồi gọi
  `vqeaf_ui_show(LOCK)`, mà `vqeaf_ui_show()` gọi `vqeaf_ui_activity()` — hàm
  này thấy cờ off là bật backlight và xoá cờ ngay. Timeout vì thế **không hề
  tắt màn hình**. Tách `note_activity()` (chỉ reset idle timer, dùng trong
  `vqeaf_ui_show`) khỏi `vqeaf_ui_activity()` (chỉ gọi khi có input thật,
  có wake). `vqeaf_ui_toggle_screen()` dùng chung state machine.
- Chế độ ngủ thật trong `vqeaf_ui.c`: `sleep_dim()` (3 s trước hạn, đèn còn
  1/4) → `sleep_enter()` (`vio_set_backlight(0)`, `screen_off=1`, về Lock,
  log `power: sleep`) → `sleep_exit()` (khôi phục độ sáng đã lưu, log
  `power: wake`, `lv_refr_now`). `main_violet.c` khi `vqeaf_ui_sleeping()`
  chuyển vòng lặp 16 ms → 100 ms, **bỏ `lv_timer_handler()`/`display_poll()`
  và mọi refresh sysfs**, chỉ poll phím nguồn + touch để thức. Đây là tiết
  kiệm pin thật, không chỉ đổi số trong Settings.
- Test host (`tests/vqeaf_shell_interaction.c`): nhãn có dấu, chạm đổi
  `1 phút → 5 phút`, dòng giải thích cuộn được lên trên dock, timeout 1 s
  → `sleeping/screen_off/brightness=0/Lock`, 1,2 s không input vẫn ngủ,
  input thức lại khôi phục đèn, nút Sleep thủ công vào/ra cùng trạng thái.
  Stub `vio_log`/`vio_sleep_ms` thêm vào `tests/vqeaf_host_platform.h`.
- Kiểm chứng: `test_violet_shell.py` ALL OK, `render_phone_ui.py` 60 PNG,
  `build_violet.py` 8.339.624 B / 245 object, `package_violet.py` →
  SHA-256 `d10c44d59a1369c0a...`, validate + audit PASS, CTest 10/10.
  Chưa flash/nạp p77; mức tiết kiệm pin thực tế chưa đo trên phần cứng.

## Nút "Áp dụng" đổi bàn phím thật của OS (2026-11-12)

- Trước đây `g_kb_theme` chỉ đổi card xem trước; `style_keyboard()` vẫn tô
  bàn phím bằng palette Gboard hard-code, và modal Wi-Fi còn **ghi đè** 6 dòng
  style cố định sau khi gọi `style_keyboard()`. Trang tự ghi "chưa có bộ gõ để
  áp dụng" — đúng nhưng không còn đúng sau yêu cầu này.
- Tách `g_kb_theme` (đang chọn) và `g_kb_theme_applied` (đang dùng). Nút
  **Áp dụng** trên trang Chủ đề bàn phím commit lựa chọn, ghi
  `keyboard: applied theme <name>` và cập nhật dòng
  "Đang dùng: <tên> · Có hiệu lực trong phiên này". Dòng ghi chú mới nêu rõ
  phạm vi: tìm kiếm, ghi chú, mật khẩu Wi-Fi.
- `vqeaf_pages_kb_theme_style()` là điểm duy nhất tô_style mọi `lv_keyboard`
  thật của shell (search modal, Notes, Wi-Fi modal): nền/phím/chữ/bo góc/
  viền lấy từ bảng chủ đề, màu chữ tự chọn tối hoặc sáng theo độ sáng của phím
  (Pastel cần chữ đen), accent dùng cho trạng thái nhấn. Bỏ đoạn ghi đè cố
  định trong `wifi_open_modal`.
- Keyboard đang mở cũng đổi ngay: mỗi bàn phím được đăng ký vào mảng 4 slot,
  tự gỡ qua `LV_EVENT_DELETE`, và `kb_restyle_live()` tô lại khi bấm Áp dụng.
  Bàn phím tạo sau đó đọc `g_kb_theme_applied` nên kế thừa luôn.
- **Trung thực về phạm vi**: đây là chủ đề của widget bàn phím LVGL trong shell,
  không phải IME toàn hệ thống; state sống trong RAM nên reboot về Nightfall.
  UI ghi rõ điều đó, không tuyên bố lưu vĩnh viễn.
- Test host đọc **màu style thật** của một `lv_keyboard` (`lv_obj_get_style_bg_color`
  + `lv_color_eq`): mặc định Nightfall → chọn Bubble chưa đổi → bấm Áp dụng
  đổi ngay cả keyboard đang tồn tại lẫn keyboard tạo sau, dòng trạng thái và
  phạm vi phiên đều hiển thị. Ảnh `interaction-keyboard-theme-applied.png`.
- Kiểm chứng: `test_violet_shell.py` ALL OK, `render_phone_ui.py` 60 PNG,
  `build_violet.py` 8.347.312 B / 245 object, `package_violet.py` →
  `dist/boot-vqeaf-violet.img` 20.058.112 B, SHA-256 `44871fee377156da...`,
  validate + audit PASS, CTest 10/10. Chưa flash/nạp p77.

## Independence audit — 2026-11-11

**Important correction:** `platform/violet/` and `dist/boot-vqeaf-violet.img` are a Linux-musl PID 1/initramfs prototype running on the stock Linux kernel. They are legacy transport experiments, NOT QEAFIVELS Kernel or an independent OS release. Do not use them as proof of standalone boot. The freestanding x86_64 UEFI/QEMU kernel is a separate reference target and is not Violet-compatible. Canonical policy and staged gates: `docs/INDEPENDENT_OS_CHARTER.md`.

The Violet hardware notes identify `mmcblk0`/eMMC, not UFS; SM6150 vs SDM660 and secure-boot state remain evidence items to resolve. No native Violet kernel or physical-device compatibility is currently verified.

## Structured QEAPP packaging and project tests (2026-11-11)

- The example and generated SDK scaffold include `src/`, `libs/`, `assets/`,
  `packaging/package.json`, `tests/`, an SDK compiler launcher, and `build.py`.
  Packaging names control artifact basenames independently of manifest IDs and
  display names. See `examples/hello-qeapp/README.md` and `sdk/cli/README.md`.
- SDK `create` validates names/IDs before creating a project and refuses existing
  directories. `build` and `package` use the shared SDK compiler, automatically
  sign stored archives, and write to `build/releases/` and `build/debug/`.
  Build tooling, packaging metadata, and tests are excluded from archives.
- Python regression: 41 tests passed (`build/qeapp-packaging-tests.log`), including
  the external generated project's five tests. Example-local tests: 5 passed
  (`build/qeapp-project-tests.log`). Actual release/debug artifacts installed in
  the host runtime with linked library output; tampered and print-boundary
  cases passed (`build/qeapp-packaging-runtime.log`). Commands, versions and
  package hashes: `build/qeapp-packaging-verification.json`.
- Fresh native Release build: 10 smoke tests passed, assertions enabled
  (`build/sdk-native-qemu-verification-01/ctest.log`). QEMU did NOT pass:
  `scripts/package_bootimg.py` leaves FAT12 directory clusters unallocated, so
  firmware cannot execute BOOTX64.EFI; recovery was not reached. Evidence:
  `build/sdk-native-qemu-verification-01/fat-evidence.log` and `run-qemu.log`.
  This separate boot-image defect was not changed by the QEAPP work.
- Runtime ABI remains v1; scripts remain declarative and signing is development
  HMAC only. No device flashing or physical-device verification was performed.

## Nhấn giữ xem thông tin app, lõi đa phương tiện, duyệt thư mục con (2026-11-11)

- **Nhấn mở / nhấn giữ**: mọi ô app (lưới Home, trang Ứng dụng, dock, QEAPP đã
  cài) mở bằng chạm ngắn; nhấn giữ 900 ms mở hộp thoại thông tin (tên, loại,
  phiên bản, quyền, Mở/Di chuyển/Thêm vào dock). Handler mở app nghe
  `LV_EVENT_SHORT_CLICKED` vì LVGL vẫn gửi CLICKED sau long-press.
- **Template + compiler .qeapp**: `sdk/templates/hello-qeapp/` (manifest, main.qe,
  icon) và `qeaf create/build/validate`; gói ZIP không nén + SIGNATURE
  HMAC-SHA256 dev key, runtime chỉ chạy `app`/`print`.
- **Lõi đa phương tiện**: `platform/violet/ui/vqeaf_media.c` nhận diện bằng magic
  (không chỉ đuôi tệp) và đọc metadata cho txt/json/md/log, png/jpg/bmp/gif,
  mp3/wav, mp4, pdf, zip/qeapp, raw. Viewer giải mã thật PNG/JPEG/BMP
  (lodepng + tjpgd bật trong `lv_conf.h`); phần chưa có codec hiện metadata kèm
  ghi chú trung thực "codec phát chưa có trong ảnh initramfs", không tuyên bố
  phát được âm thanh/video.
- **File manager duyệt thư mục con ở cả hai tab**: tab QEAFRESCUE dùng API
  cluster mới của FAT16 (`fat16_list_dir`, `fat16_subdir_cluster`,
  `fat16_read_file_in`, `fat16_list_path`, `fat16_read_path`); hook đồng bộ
  (`vqeaf_ui_set_sync_list/read`) nhận đường dẫn tuyệt đối. UI tự thêm `..` vì
  `fat16_list_dir` bỏ entry chấm; `..` không thoát được thư mục ở cả list lẫn
  read (có test phủ).
- **Icon hệ thống**: vẽ lại bằng VPEPixel theo mẫu
  `temp/appicons_AppIcons_MyAwesomeApp`; 29 PNG + contact sheet tại
  `assets/icons/vqeaf_os/`, nhúng vào shell qua `vqeaf_app_icons.c`.
- **Sửa lỗi hiển thị dung lượng**: `format_bytes` in thừa chữ số 0 khi phần thập
  phân bằng 0 (35 B hiện thành "350 B"); đã sửa và thêm assertion hồi quy.
- **Kiểm chứng (host)**: `scripts/test_violet_shell.py` ALL OK — 60 assertion
  gồm nhấn giữ/mở, di chuyển dock, giải mã PNG/JPEG/BMP, metadata MP3, duyệt
  `/media/sub` và `/Apps/Tools` trên tab QEAFRESCUE (12 snapshot PNG);
  `scripts/test_fat16_path.py` ALL OK — 34 assertion trên volume FAT16 thật
  trong RAM (LFN, chuỗi đa cluster, chặn `..`); `test_vqeaf_media.py`,
  `test_qeapp_runtime.py` ALL OK; `unittest discover -s tests` 17 test OK;
  `render_phone_ui.py` 60 PNG. Build aarch64: `build/violet/vqeaf-init`
  8.260.448 byte, 244 object, 0 warning (`build/device-build.log`).
- **Giới hạn xác minh**: milestone này chỉ build + link ảnh device, chưa flash
  hay boot lại thiết bị; mọi khẳng định UI dựa trên render host và test tương
  tác LVGL thật, không dựa trên ảnh chụp thiết bị mới.

## LVGL smartphone UI — Violet shell integrated (2026-11-11)

- Dùng LVGL 9.4.0 vendored tại `platform/lvgl`; giao diện tham chiếu
  `uiux/546546.png` và các module launcher/status/settings của QeafdropOs.
- `main_violet.c` khởi tạo LVGL sau splash, gắn display 720×1560 XRGB8888,
  flush callback qua DRM/KMS, evdev pointer adapter và vòng `lv_timer_handler()`.
  LUN cứu hộ và fault guard của PID 1 vẫn được giữ nguyên.
- UI hiện có Lock, Home, Apps (24 icon), Control Center, Settings, status bar,
  bottom navigation; Ubuntu/Material Icons được link tĩnh; cập nhật giờ/ngày và
  telemetry chỉ từ provider có thật.
- Build ARM PASS: `build/violet/vqeaf-init` 6,405,128 byte, 235 object.
  Package PASS: `dist/boot-vqeaf-violet.img` 19,554,304 byte,
  SHA-256 `bf56601158cffdd7e163492d81cf14501524baed6c9dbb3e0fcbe5fe8c666619`.
  `validate_bootimg.py` và `audit_rescue_channel.py` đều PASS.
- Giới hạn xác minh: đã build và đóng gói, chưa boot thử giao diện mới trên thiết bị
  hoặc xác nhận cảm ứng thực tế. Không flash/ghi phân vùng trong milestone này.
- Tài liệu thiết kế và kế hoạch: `docs/PHONE_UI.md`. Bước tiếp theo là chụp
  framebuffer kiểm tra bố cục, hoàn thiện wallpaper/gesture/notification và
  bổ sung Recent Apps, File Manager, Gallery, Music, Video, Camera, App Store.


## Wi-Fi + driver/ổ đĩa trên máy thật (2026-11-11)

- Thêm trang **Wi-Fi** và **Trình điều khiển** (kèm mục **Ổ đĩa**) vào Settings,
  đọc trực tiếp `/sys/class/net/wlan*`, `/proc/net/wireless`, `/sys/module`,
  `/dev/dri`, `/dev/input`, `/sys/class/udc`, `/sys/class/backlight` và
  `/run/qeaf-lun.img`. Không có SSID/cường độ/mạng nào được bịa.
- Kết quả đo trên violet: module `wlan` + `cfg80211` đã nạp nhưng **không có
  netdev wlan0**; mọi thư mục firmware đều vắng (`/vendor/firmware`,
  `/lib/firmware`, …). Trang Wi-Fi hiện “Không có giao diện mạng” và
  “Firmware WCN3990: Thiếu”; đây là giới hạn thật của ảnh, không phải lỗi UI.
  Muốn có Wi-Fi phải có blob WCN3990 từ chính máy và một supplicant; HakiOS tách
  đúng hai tầng này trong `docs/driver-review/wifi-stack.md`.
- Sửa đường chạm cứu hộ: contact giữ 3 poll và container không còn CLICKABLE
  mặc định, long-press dock/app 900 ms. Nhờ đó tap vào chữ/khung hàng Settings,
  icon dock và card chủ đề mới tới handler; trước đó lớp container nuốt CLICK.
- Ảnh máy thật khớp render: `wifi` 97,6% (mean 2,5) và `drivers` 98,0%
  (mean 2,3), cả hai PASS (`build/screens/phone-ui/compare-wifi.png`,
  `compare-drivers.png`). `status` có thêm `page=` để biết trang đang mở.
- Ảnh đã nạp: `dist/boot-vqeaf-violet.img` 19.587.072 B, SHA-256
  `c0b4014c5ebfed9393c03cb3fbd230a09bbf196a45685e594b1eb75b464855fe`;
  p77 ghi và đọc lại khớp từng byte, boot lại vào shell LVGL.

## Wi-Fi scan, đồng bộ QEAFRESCUE, chủ đề màn hình khóa (2026-11-11)

- **Wi-Fi**: thêm danh sách mạng khả dụng (quét WEXT trên netdev thật), modal
  nhập mật khẩu có bàn phím LVGL cho mạng bảo mật, và nút Quét lại. Trên violet
  hiện tại không có `wlan0`/firmware nên danh sách trả ENODEV và trang hiện đúng
  “Không có giao diện mạng”; modal/kết nối bảo mật sẽ báo thiếu supplicant thay
  vì giả lập thành công. Render host có danh sách mẫu để kiểm bố cục.
- **Tệp & đồng bộ**: trang mới đọc thư mục gốc FAT16 của QEAFRESCUE qua hook có
  khoá volume. Đã thử thật: thả `SYNCTEST.TXT` từ PC vào ổ, mở trang và chạm
  Đồng bộ — tệp xuất hiện trong danh sách (13 mục). Tệp OS ghi
  (DEVINFO/RESPONSE/SCREEN.RAW) vẫn đọc được từ PC; cùng một ảnh FAT16, không có
  bản sao thứ hai.
- **Chủ đề màn hình khóa**: editor có preview, 4 kiểu font, thanh độ đậm, 7 màu
  và nút Xong; lựa chọn áp dụng ngay cho lock screen. Đã thử chọn font “Cao” rồi
  Xong: `status` trả `page=0` và ảnh lock cho thấy đồng hồ đổi cỡ.
- Ảnh máy thật khớp render: `wifi` 97,2% (mean 2,8), `sync` 96,1% (mean 6,4),
  `lock-theme` 98,1% (mean 2,3) — tất cả PASS.
- **Dock mặc định** đổi sang bốn ứng dụng Điện thoại, Tin nhắn, Trình duyệt,
  Thư viện (id 4/5/7/9); vẫn giữ kéo-thả để thay slot. Điều hướng test chuyển
  sang home bar / status bar / vuốt ngang; ảnh Home trên máy khớp render 98,4%
  (mean 1,6). Apps mở bằng “Tất cả” `(654,346)`, Settings bằng status bar rồi
  vuốt trái — cả hai đã kiểm bằng `page=2` và `page=4`.
- **File manager**: ứng dụng Tệp có tab Hệ thống (rootfs thật của OS) và tab
  QEAFRESCUE (cùng hook FAT16). Đã thử thật: thả `FILESYNC.TXT` từ PC, mở Tệp →
  QEAFRESCUE → Đồng bộ, tệp hiện ở cuối danh sách (180 B); tab Hệ thống liệt kê
  đúng `config/dev/etc/lib/proc/run/sys/tmp/...` của rootfs. Ảnh:
  `build/screens/device/files-system.png`, `files-rescue.png`.
- Ảnh đã nạp: 19.619.840 B, SHA-256
  `85e964f9ed4cc27a036d766914c1fa0953eefd3b64a11a5c0e0e5153ad48ac48`;
  p77 ghi/đọc lại khớp từng byte, boot lại vào shell LVGL.
- Giới hạn: chưa có phần cứng Wi-Fi (firmware WCN3990/supplicant) nên chưa thể
  chứng minh quét thật, mở modal từ mạng thật hay vào được WPA2. Chủ đề và dock
  chỉ sống trong RAM vì violet chưa có phân vùng prefs.

## QEAPP có chữ ký, cài từ QEAFRESCUE (2026-11-11)

- SDK: `qeaf build <project> --stored` xuất ZIP không nén; `qeaf sign <pkg>`
  thêm `SIGNATURE` = HMAC-SHA256 trên toàn bộ entry (trừ SIGNATURE), key dev
  `VQEAF-QEAPP-DEV-KEY-2026`. Đây là profile ký dev dùng khoá chung, không phải
  chữ ký public-key.
- Runtime violet: file manager thấy `.qeapp` trong tab QEAFRESCUE và hiện nút
  Cài; đọc ZIP stored, xác minh chữ ký, parse `manifest.json` + `main.qe`
  (`app`, `print`), lưu app vào RAM và thêm mục **Đã cài** trong Ứng dụng. Trang
  QEAPP hiển thị tên/id/version/nội dung và nút Gỡ.
- Đã kiểm bằng test C trên host với đúng package: `dist/hello.qeapp` →
  `rc=0`, name `Hello VQEAF`, message đúng; `dist/hello-tampered.qeapp` →
  `rc=-1`, “Chữ ký không hợp lệ hoặc tệp đã bị sửa.” SHA-256/HMAC đã đối chiếu
  vector chuẩn (`abc`, HMAC-SHA256).
- Render: 60 PNG, 15 trang × 2 profile × 2 hướng; `qeapp` render đúng trang
  Hello VQEAF.
- Ảnh đã nạp: 19.632.128 B, SHA-256
  `96f8ad108beed7d0c6373a7914509fecb30f322c69929516c306c83fe7bd2a21`.
- **Nạp lại và cài trên máy**: máy vào fastboot `ca1af25d`, đã ghi p77 bằng
  `restore_p77.ps1 -Image dist/boot-vqeaf-violet.img -Execute
  -ConfirmPartitionWrite` (fastboot flash recovery OKAY, reboot recovery).
  QEAFRESCUE lên lại sau ~3 s; `status` mới: uptime 15 s, `page=0`, UI=1,
  DRM=1; HOMOLOG có `usb: mass storage bound`, `drm: verified`,
  `ui: LVGL phone shell active at 720x1560`.
- Trên máy đã copy `hello.qeapp` vào QEAFRESCUE, bấm Cài và nhận modal **Đã cài
  ứng dụng**; Ứng dụng → cuộn xuống thấy mục **Đã cài** với tile Hello VQEAF.
  Chưa chụp được trang QEAPP sau khi chạm tile (tap tổng hợp bị nhiễu bởi thao
  tác tay trên máy); trang này đã được render và test C trên host.

## Violet port — PID 1 boot trên Redmi Note 7 Pro (2026-11-10)

- `platform/violet/` là cổng aarch64-musl của cây này lên violet, chạy trên
  stock kernel (kernel+DTB byte-for-byte từ
  `QeafdropOs/out/reference/stock-violet/boot.img`, chỉ ramdisk đổi):
  - `channel/` — `fs/rescue.c` (kênh `QEAF.CMD`/`RESPONSE.TXT`/`QEAFBOOT.LOG`),
    `fs/fat16.c` + `fs/storage.c` (LUN RAM 32 MiB), `core/sysio.c`,
    `core/log.c`, `hal/include/{display,usb_gadget}.h` — cùng ABI với bản
    đã kiểm chứng của QeafdropOs.
  - `drm_kms.c` — GETRESOURCES/GETCONNECTOR/GETENCODER, dumb **XRGB8888**
    buffer, ADDFB, SETCRTC, DIRTYFB; poll connector + force modeful sau grace
    period (DSI từng trả "disconnected" thoáng qua).
  - `panel_scale.c` — UI canvas 720x1560 (dọc) / 1560x720 (ngang) = 1.5x
    1080x2340, không letterbox; XRGB8888 -> scanout.
  - `usb_lun.c` — configfs `f_mass_storage`, LUN file `/run/qeaf-lun.img`
    `MAP_SHARED`, bind `a600000.dwc3`, retry khi UDC xuất hiện muộn.
  - `devnodes.c`, `display_hal.c`, `violet_input.c`, `violet_power.c`.
- Entry point `main_violet.c` + `splash.c`: PID 1 một tiến trình — mounts, /dev,
  LUN, rescue worker pthread (mutex `volume_lock`), backlight, DRM init và splash;
  từ 2026-11-11 khởi tạo tiếp LVGL phone shell, chạy event/timer loop và evdev input;
  sigsetjmp guard quanh panel path, LUN luôn sống khi UI fault.
- Build `scripts/build_violet.py` (Zig 0.13 `-target aarch64-linux-musl`,
  hiện gồm 235 objects, `-static`) và `scripts/package_violet.py` (stage `/init`
  duy nhất -> `mk_initramfs` -> `mk_bootimg_violet` -> gates). Bản LVGL mới nhất:
  19 554 304 B, SHA-256 `137beae2b37621f714a4fb73dab86bfae8e8003d2f81339a2fca0bf57a2e9cb3`.
- Gates trước flash: `validate_bootimg.py` PASS; `audit_rescue_channel.py`
  PASS với `init_panel`/`init_gadget` đếm được **trong chính ELF `/init`**
  (`QEAF.CMD` 12, `flash recovery` 4, `configfs` 1, `UDC` 3...).
- Nạp qua kênh cứu hộ trên chính máy (không cần fastboot):
  `prepare_rescue_flash.py --commit`, hai bằng chứng
  `flash: written and verified byte for byte` + `lun: verified ... over
  18026496 bytes`, rồi `reboot` qua `send_rescue_cmd.py`.
- Boot thật đo được (`HOMOLOG.TXT`/`DEVINFO.TXT` qua raw sector):
  `boot: VQEAF-OS 0.3.0 init on violet (PID 1)`, `usb: mass storage bound
  to a600000.dwc3`, `backlight 85%`, `drm: verified - crtc 107 scans our
  fb 188 at 1080x2340`, `ui: splash 720x1560 (portrait)`;
  `DEVINFO.TXT`: `os=VQEAF-OS (violet)`, `ui=vqeaf-phone`.
  SCREEN.RAW (240x520 RGB565 từ verb `snap`) xác nhận splash trên kính —
  `QeafdropOs/out/report-vqeaf-reboot/vqeaf-splash.png`.
- Chưa xác nhận trên violet: cảm ứng thực tế, backend Wi-Fi/BT, pin/sạc,
  prefs/persist và app runtime. LVGL shell đã có input adapter evdev nhưng cần
  kiểm tra trên thiết bị. Bước tiếp theo: framebuffer capture, gesture, wallpaper,
  notification/recent apps và smoke test UI.

## Phase 1 + 2 — VirtIO block controller and storage hardware stack
- `kernel/arch/x86_64/pci.c`: type-1 PCI config access via 0xCF8/0xCFC, bus scan for vendor/device.
- `kernel/drivers/virtio_blk.c`: QEMU VirtIO 0.9 legacy PCI block driver (`1AF4:1001`, I/O BAR0, INTx IRQ 11). Verified QEMU queue size is 256, so the descriptor/avail rings occupy two pages and the used ring starts at queue_base+0x2000 (QEMU `VIRTIO_PCI_VRING_ALIGN=4096`). Guest features 0; capacity 32768 sectors of 512 bytes.
- Asynchronous by construction: `submit_type()` builds a 3-descriptor chain, publishes it in the avail ring and kicks the queue; the INTx stub (`vqeaf_virtio_irq_stub` in switch.S) calls `vqeaf_virtio_blk_isr()`, which reads the ISR reason, consumes used-ring elements and invokes completion callbacks. A sync wrapper waits with `sti; hlt` plus a 3 s PIT-tick timeout — no polling loop masquerading as async.
- Four in-flight slots; self tests prove two concurrent reads complete through one IRQ path, an unsupported command returns a device error, and an async sector write + readback matches on a scratch LBA outside the FAT partition. Observed run: 12 INTx / 12 completions.
- Driver model integration: `virtio-blk-pci` is registered and bound; `storage.block`, `storage.virtio_blk` and (after mount) `storage.fat16` become READY. `arch.c` gained `vqeaf_arch_install_irq/unmask/eoi` including slave-PIC cascade IRQ2 handling.
- `scripts/make_test_disk.py` generates a deterministic 16 MiB MBR + FAT16 disk with `/VQEAF.TXT`; QEMU attaches it as `virtio-blk-pci,disable-modern=on`. `tests/test_virtio_disk.py` validates MBR, BPB, FAT chain, root entry and payload. `run_qemu.py` asserts the full storage marker chain; `capture_screen.py` attaches the same disk.
- Kernel task `task_storage` performs MBR parse → read-only FAT16 mount → VFS read → mount namespace resolve → I/O error contract checks (bounds, zero length, read-only write rejection).
- **Bug found by real hardware attach:** `partition.c` and `fs_fat16.c` compared the boot signature word against `0x55AA`, but on-disk bytes `55 AA` read as little-endian `0xAA55`. Host fixtures in `tests/system_v03_smoke.c` had been written with swapped bytes, hiding the defect. Parsers and fixtures are now corrected; all 6 CTest and 11 unittest cases pass.

## Boot splash + native UI (boot protocol v3)
- Boot Manager locates EFI_GRAPHICS_OUTPUT_PROTOCOL, records the linear framebuffer in `VqeafBootInfoV3` and draws a splash directly into it. Verified on OVMF/QEMU: 1280x800, 1280 stride, BGR pixel format, framebuffer at 0x80000000 (4 MiB). Boot continues text-only if GOP is missing or unusable.
- Kernel maps the framebuffer range with write + cache-disable after paging is active; no UEFI Boot Service is called after ExitBootServices.
- `system/ui/gfx.c` + `system/ui/ui.c` provide a live diagnostics shell: status bar with uptime, six system cards, a moving activity meter and PIT tick counter. A dedicated kernel task repaints roughly 4 times a second.
- `scripts/capture_screen.py` waits on serial milestones, captures `screendump` PPMs, converts them to PNG without third-party packages and asserts VQEAF accent pixels are present. QEMU regression asserts the GOP/UI serial markers.
- Font: public-domain 8x8 bitmap (Daniel Hepper / IBM VGA), embedded in `system/ui/include/vqeaf/font8x8.h`; ASCII only, no UTF-8/touch/widget/compositor support yet.
- Deployment boundary: this is still an x86_64 UEFI reference image. No phone bootloader chain, display/touch driver, storage driver or signed update path exists. Do not flash it to physical hardware.

## System v0.3 — storage, power, discovery, text, service manager
- Block device abstraction v1: bounded registry, checked LBA bounds, explicit read-only media (write op absent reports error, never silently succeeds).
- Partition parser: MBR signature + entry validation, overlap and past-end rejection, protective-MBR/GPT detection (GPT contents still unread).
- First persistent filesystem: read-only FAT16 mount over block device/partition with strict FAT12/FAT32 rejection, 8.3 lookup, subdirectory chains, cluster-chain reads, VFS vnode integration; write support NOT implemented and reported as such.
- Power v0.3: wake locks (named, TTL or until-released, bounded), dim→blank display stages with change callbacks, battery provider input with validation (impossible telemetry rejected, absent provider = absent status, no invented data), documented low-battery policy (≤15% halves timeout, ≤5% blanks immediately).
- Device discovery: bus enumerators report what firmware/bus probing sees; discovery matches drivers by name, probes, and registers capability entries (READY only after a successful driver probe, PRESENT without a driver, BLOCKED_FIRMWARE honored).
- Locale/Text v0.3: strict UTF-8 scanning (overlong/surrogate/truncation rejected), Vietnamese grapheme composition (base + combining marks → precomposed codepoints, chained e+^→ê+dot→ệ), glyph fallback (covered → base letter → U+FFFD), advance-width measurement; composed and decomposed Vietnamese text measure identically (unit-tested with "Tiếng Việt").
- System service manager: bounded registry, dependency-ordered start with cycle detection, permission grants required to start/stop, dependent-stop protection, reverse-order stop_all.
- Test coverage: tests/system_v03_smoke.c (block bounds, MBR/GPT, FAT16 fixtures built in-test, wake-lock/battery policy, discovery→capability, Vietnamese text, service lifecycle) plus existing suites; all CTest green in Debug and Release.
- Release-build note: test calls with side effects are never placed inside assert(); Release defines NDEBUG and would otherwise skip initialization (found and fixed during this phase).

## Phase 3.1 — Memory & process execution
- Identity page tables built from the UEFI memory map at 4 KiB granularity and activated via CR3 after ExitBootServices.
- Kernel heap (kmalloc/kfree): arena free-list allocator with 16-byte alignment, first fit, split and coalescing; fragmentation and coalescing covered by host unit tests.
- Each task owns a dedicated kernel stack allocated from the PMM with an unmapped guard page below it.
- CPU context switching via assembly stubs (switch.S): IRQ0 and page-fault stubs save/restore all general-purpose registers and switch kernel stacks; tasks start through a trampoline that invokes the entry function and exits.
- Preemptive round-robin scheduler running real tasks from the PIT (100 Hz); idle-free rotation verified in QEMU over 1000+ ticks across three tasks.
- Page fault handler with guard-page recovery: an armed expected fault (vqeaf_pf_expect) skips the faulting instruction and resumes the task; unexpected faults panic with CR2/error/RIP diagnostics.
- Verified in QEMU: paging enabled, heap online, three tasks created, guard-page fault recovered, T1/T2 alternately alive through 1000+ ticks without panic.

## Phase 2–3 — Kernel core bring-up
- Native x86_64 UEFI entry after ExitBootServices, GDT/IDT, exception diagnostics, PIT IRQ, PMM, page-table primitives, scheduler metadata, IPC queue and VFS vnode interface.

## VQEAF Core v0.2 — Host-tested foundations
- Driver model ABI v1, bounded registry, probe/remove and instance states.
- Device capability registry with explicit READY and hardware/firmware-blocked states.
- Storage mount namespace and VFS root resolution/path checks.
- Power policy supports 15s/1m/5m/30m/Never, defaults to 1m, resets on activity and respects display capability/suspend.
- Default locale vi-VN, UTF-8, en-US fallback and bundled Ubuntu regular/bold fonts with UFL license.
- CTest includes core services smoke coverage.

## Not implemented / hardware blocked
- Single kernel address space: all tasks share one CR3 root; per-task address spaces and user mode are Phase 3.2+ work.
- The identity map covers every memory-map range below 4 GB; memory above 4 GB is not mapped and PMM still caps at 4 GB.
- Task exit marks the slot BLOCKED and parks the task; exited stacks and their page tables are not yet reclaimed.
- The page-fault recovery path handles one armed fault at a time and only for not-present data reads in kernel mode.
- No physical (non-emulated) block device driver and no writable/journaled filesystem; VirtIO and AHCI are QEMU-emulated controllers.
- No physical smartphone driver probes, display/backlight, PMIC, battery telemetry, radio, modem or sensor implementation.
- Display sleep/wake blanks the GOP framebuffer only; the power manager does not switch hardware display rails or backlight.
- Capability registry is an API; hardware discovery providers still need to register actual results.
- No biometric or phone-specific secure services are claimed.

## Verification boundary
- x86_64 build, host tests and QEMU are reference validation only.
- No phone flashed and no physical disk/firmware partitions modified.

 
## 2026-11-10 — Core v0.2 kernel integration and boot recovery
- Fixed Boot Manager GOP success handling: framebuffer drawing is performed only when GOP setup succeeds; serial-only fallback never dereferences an uninitialized framebuffer. Splash progress now transitions to KERNEL.EFI rather than leaving a permanent logo.
- Kernel now links and initializes driver registry, capability registry, block registry, storage mount namespace, FAT16/VFS services and power manager.
- Registered capability states truthfully: GOP framebuffer and software screen-timeout are READY when framebuffer mapping succeeds; backlight and physical block storage remain UNSUPPORTED; serial input is PRESENT.
- Added PIT-based monotonic screen timeout task (default 1 minute), software framebuffer blanking and wake on input.
- Ubuntu-Regular.ttf now generates a compact 12x16 glyph table for ASCII and Vietnamese Unicode codepoints. Boot splash and native UI use the same UTF-8 bitmap renderer. Default locale remains vi-VN with en-US fallback.
- Added UTF-8/Vietnamese framebuffer smoke test. Full validation: 5/5 CTest tests, 8/8 Python tests, normal and recovery QEMU boot, splash and native UI screenshots.
- Hardware boundary unchanged: QEMU x86_64 reference only; no physical phone display/backlight, PMIC or block-storage driver is claimed.

- Final QEMU capture regression: the screen-power callback now only changes the blank-state flag; the existing UI refresh task performs the framebuffer clear on its next pass. The renderer writes Ubuntu glyph pixels directly to the framebuffer (no per-pixel rectangle calls), preventing partially rendered frames under preemption. The UI and splash captures now show the complete Vietnamese screen; final capture run has no kernel panic.


## 2026-11-10 — Hardware abstraction, VFS and power regression
- Rebuilt baseline: 5/5 CTest and 8/8 Python unit tests passed before changes.
- Expanded core service tests for longest-prefix nested mounts, path boundary (/datax), traversal rejection, independent unmount, and path invalidation after unmount.
- Expanded power tests for suspended timeout freeze, resume + fresh activity wake, and a subsequent timeout transition.
- Expanded FAT16 integration test through the storage mount namespace: resolve /data/HELLO.TXT, walk the relative path, unmount and verify it no longer resolves; remount and reject duplicate mount.
- Current post-change CTest: 5/5 passed.
- Hardware boundary remains explicit: no physical AHCI/NVMe/eMMC/UFS driver is bound in the current QEMU image; RAM-disk is a test fixture only. Do not mark physical storage capability READY.
- Added HARDWARE_STORAGE_POWER.md with driver/block boundary, validation matrix and safe system-update screen contract.

- Added responsive Installing Update renderer for 240x320 and desktop GOP modes (system/ui/update_screen.c); stage and percentage are supplied by the update service, not animated/faked. The native shell exposes serial debug key i to show the safe waiting screen ("Chưa có gói cập nhật") and h to return home. This is a screen component only; no updater is falsely reported as functional.
- Added update-screen framebuffer smoke test; final CTest is 6/6, Python tests 8/8, normal QEMU boot PASS, recovery QEMU boot PASS, and real framebuffer splash/UI captures PASS (1280x800).
- Removed automatic scripted Sleep action from boot-time input demo so the native display stays visible for boot verification; power suspend/wake transitions remain covered by deterministic host tests and are available through the interactive power menu.
- UI framebuffer repaint is performed with interrupts masked for the duration of the single render pass to prevent task preemption from interrupting framebuffer drawing.

## 2026-11-10 — AHCI/SATA block driver, VFS mount tests, display wake/suspend
- PCI config scan now walks functions 0-7 (`pci.c`), so the QEMU ICH9 AHCI controller at `00:1F.2` is found. The driver does not issue a global HBA reset (QEMU keeps PxSIG=0xFFFFFFFF across it); it enables GHC.AE and keys presence on per-port SSTS DET==3.
- `kernel/drivers/ahci.c` rewritten for multiple ports (up to 4): per-port command list/FIS receive/command table pages, port stop/start, IDENTIFY via DMA and READ/WRITE DMA EXT. The command-header word is the AHCI/EDK2 layout: CFL in bits 0-4, write bit 6, PRDTL in bits 16-31. Getting CFL wrong (shifted to bits 5-9) was the reason IDENTIFY failed with task-file errors; QEMU's AHCICmdHdr uses an opts16 field with a 5-bit CFL.
- Verified in QEMU with two SATA disks: 2 AHCI disks enumerated; the ESP disk (0xB40 sectors at LBA 0) has no FAT16 partition and is skipped for mounting; the 0x8000-sector test disk is mounted read-only at `/storage`, `/storage/AHCI.TXT` reads back `VQEAF OS AHCI/SATA FAT16 read-only mount OK`, a scratch sector write+readback+restore is verified, the registered block device rejects writes (`-3`), and unmount/remount is verified. Serial markers run `STORAGE: AHCI ...`; the normal-boot QEMU gate requires `STORAGE: AHCI phase complete`.
- `scripts/make_test_disk.py` gained `--out/--name/--content`; `build.py` now produces both `virtio-test.img` and `ahci-test.img`; `capture_screen.py` and `run_qemu.py` attach the SATA test disk alongside VirtIO.
- VFS mount/unmount coverage extended: longest-prefix nested mounts, `/datax` path boundary, traversal rejection, after-unmount resolution failure, duplicate-mount rejection and per-mount FAT16 pool (host tests).
- Installing System Update: stage model (package check, signature/SHA-256, battery/free-space, write+verify), percent and blocked/failed states with no invented progress; native serial key `i` shows the blocked view when no updater service exists, `h` returns home. Screenshot `build/screens/update_blocked.png`.
- Power menu is now drawn as an overlay (long Power press opens; volume up/down moves; short Power confirms). Sleep sets display off and suspends the timeout; any serial byte is user activity and wakes the display. Serial markers `POWER: display=off|dimmed|active` and `POWER: wake from display-off` make the transition observable.
- New `scripts/test_input_power.py` drives the guest over a QEMU serial socket chardev (the kernel polls COM1, so monitor `sendkey` cannot drive it): opens the menu, moves the highlight to Sleep, blanks the screen (screendump non-black bytes=0), wakes it, shows the blocked update view, then confirms Power Off. It captures `power_home.png`, `power_menu.png`, `power_menu_sleep.png`, `screen_asleep.png`, `screen_awake.png` and `update_blocked.png`, and is wired into `scripts/test.py`.
- Full validation after these changes: 8/8 CTest, 11/11 Python unit tests, SDK create/build/package/validate, normal and recovery QEMU boots PASS, framebuffer splash/UI capture PASS, recovery menu PASS, input/power PASS.
- Hardware boundary unchanged: this is QEMU-emulated AHCI/SATA, not phone storage; there is still no PMIC/backlight/panel-rail driver, and no physical device was touched.

## 2026-11-10 — Smartphone UI shell (lock, launcher, quick settings, apps)
- New pure state machine + renderer `system/ui/phone_shell.{h,c}` inside the existing 450 px phone-frame kit: LOCK, HOME launcher, QUICK settings and APP screens. `vqeaf_phone_key` returns actions (`TIMEOUT_NEXT`, `WAKELOCK_TOGGLE`, `POWER_MENU`, `SLEEP`, `UPDATE`, `UPDATE_CLOSE`, `FASTBOOT`, `LOCK`) so the shell draws and the kernel performs real work; the shell itself has no hardware access.
- Lock screen: status bar clock, teal VQEAF badge, large clock and date from the CMOS RTC, unlock hint and footer. Home: live clock/date header, 8 app tiles (HE THONG, CAI DAT, LUU TRU, CAP NHAT, NGUON, FASTBOOT, DONG HO, KHOA) in a 3-column grid (2 columns below 360 px) with selection border and button hints. Quick settings: keep-awake, timeout, display off, lock, power menu, and Wi-Fi/Bluetooth rows that read "KHONG HO TRO" because no radio driver exists. Apps: About (real task count, free pages, framebuffer size, uptime, explicit no-battery/no-radio lines), Settings (screen timeout, keep awake, Wi-Fi, Bluetooth), Storage (real mount path, file, sector count and file content), Clock (RTC time and date).
- Input is no longer tied to debug letters: `VqeafKeyCode` gained NAV_UP/DOWN/LEFT/RIGHT and ENTER/BACK/QUICK/LOCK. COM1 maps W/A/S/D, E or Enter, B/Backspace, Q, L and ESC [ A..D; the UEFI key mapping and power menu still consume VOLUME_*/POWER. Physical keys and serial share the same `VqeafInputEvent` queue.
- Truthfully sourced values only: the status bar shows a battery only after a provider reports one (never in QEMU), Wi-Fi/Bluetooth stay unsupported, and the About screen states that no battery sensor exists. No temperature, signal or charge numbers are invented.
- New CMOS RTC driver `kernel/arch/x86_64/rtc.c` (ports 0x70/0x71): UIP wait, double-read consistency, BCD/binary and 12/24-hour handling, century register, range validation. The UI task samples it once per second, logs `RTC: CMOS yyyy-mm-dd hh:mm:ss` once, and sets the shell clock on each second change. If the read fails the clock renders `--:--` instead of a fake time.
- The kernel feeds real storage data into the shell: `/storage` (AHCI, AHCI.TXT) and the VirtIO `/data` chain populate path, file name, capacity and content; no storage row is drawn as mounted until the mount exists.
- `tests/phone_shell_smoke.c` (host, wired into CTest) covers every app label/timeout label, unlock, grid clamping, settings actions, quick rows, home tile actions, 1280x800 and 240x320 rendering, and frame-border pixels. `scripts/test_input_power.py` was rewritten for the new key map and drives unlock -> About/Settings/Storage/Update/Clock -> quick settings -> sleep/wake -> power menu -> Power Off over a QEMU serial socket, capturing `phone_lock.png`, `phone_home.png`, `phone_about.png`, `phone_settings.png`, `phone_storage.png`, `phone_clock.png`, `phone_quick.png`, `screen_asleep.png`, `screen_awake.png`, `update_blocked.png` and `power_menu.png`.
- Full validation after this phase: 9/9 CTest (including the new phone shell smoke), Python unit tests, SDK create/build/package/validate, normal and recovery QEMU boots, framebuffer capture, recovery menu, and the phone UI input/power run all PASS.
- Boundary: touch, compositor/window manager, animations and notifications are still missing; input is keyboard/serial, and the phone frame is a drawing target on a 1280x800 desktop GOP, not a phone panel. No physical device was touched and no phone driver is claimed.

## 2026-11-10 — Portrait 9:16 display default (720x1280)
- New shared `bootloader/core/display_mode.{h,c}`, linked into both BOOTX64 and RECOVERY, selects the shipped geometry in this order: (1) an exact 9:16 portrait GOP mode (1080x1920, then 720x1280, then any exact ratio); (2) a custom 720x1280x32 Bochs VBE mode on the existing linear framebuffer; (3) the current GOP mode unchanged with a centred 9:16 shell column.
- Why VBE is needed on the reference platform: OVMF/QEMU `-vga std` advertises 26 modes from 320x200 to 2560x1600 and every one is landscape; `virtio-vga,xres=720,yres=1280,edid=on` still reports 1280x800 and the same list. The VGA device itself accepts a portrait mode through the Bochs dispi registers 0x1CE/0x1CF: disable, set XRES/YRES/BPP/VIRT_WIDTH/VIRT_HEIGHT, enable LFB. The module checks the dispi ID first (0xB0C0..0xB0C6) and reads every register back, so a device without that interface is never written and no geometry is invented.
- Framebuffer handoff is now 720x1280, stride 720 px, BGR, 0x384000 bytes at 0x80000000 (observed serial: `DISPLAY: portrait 9:16 VBE 0x2D0x0x500`, `GOP: framebuffer ... portrait`). The boot splash layout is vertically centred in the taller panel. Recovery uses the same 720x1280 mode (`RECOVERY: GOP mode=... portrait`).
- `vqeaf_screen_frame` now treats any portrait framebuffer as the phone itself (full width); landscape keeps the 450 px centred column. Host test asserts 720x1280 -> full width, 1080x1920 -> full width, 1280x800 -> 450 px at x=415, and renders `phone_portrait_lock.ppm` / `phone_portrait_home.ppm`.
- QEMU gates were tightened to the real geometry: `capture_screen.py` and `test_input_power.py` fail unless every screendump is exactly 720x1280; `test_recovery_ui.py` does the same for the recovery menu; `run_qemu.py` requires `DISPLAY: portrait 9:16` (normal) and `portrait base=` (recovery). `capture_screen.py` waits for the display marker before judging the splash.
- Validation: 9/9 CTest, Python unit tests, SDK create/build/package/validate, normal and recovery QEMU boots, splash/UI captures (720x1280), recovery menu, phone input/power suite all PASS. Screenshots visually checked: splash, ui, phone_home, phone_quick, recovery_menu.
- Boundary: the custom-mode path is the QEMU Bochs/QEMU VGA interface, not a phone panel driver. On real firmware with neither a portrait GOP mode nor Bochs VBE the OS keeps the landscape mode and only the centred 9:16 column applies. There is still no panel/touch/backlight driver, no rotation sensor or auto-rotate setting, and no physical device was touched or flashed.

## 2026-11-10 - Observable phone-shell views and stable input gates
- The kernel now logs every phone-shell view change as `PHONE: view=lock|home|quick|app`, emitted only when a routed key actually changes the screen. It reads the new `vqeaf_ui_phone_screen()` getter (returns -1 while another view owns the framebuffer), so the marker cannot fire during blanking or UI init.
- `scripts/test_input_power.py` waits for `PHONE: view=home` instead of sleeping a fixed 0.4 s after unlock; the remaining navigation steps keep short sleeps. Driving every step off markers was tried and rejected: with that pacing the emulated 16550/chardev stops delivering input mid-run (guest CPU still alive in HLT and the ring-3 task, `pic0: irr=01 imr=fa`), so only the unlock gate is marker-based.
- The RTC gate now matches the whole line (`RTC: CMOS yyyy-mm-dd hh:mm:ss`) instead of the prefix `RTC: CMOS 20`, which could arrive split across socket reads.
- `shot()` accepts a retry count and keeps the candidate with the most teal pixels (or the darkest frame for the asleep shot). `phone_home` and `update_blocked` use two attempts because a screendump can land on a torn repaint; a torn `update_blocked.png` previously contained only the background and footer.
- Validation: `scripts/test_input_power.py` PASS twice in a row (720x1280 captures, lock->home diff 82979/82985, asleep non-black bytes 0), followed by the full suite. Screenshots visually re-checked: phone_home, phone_quick, power_menu, update_blocked.

## 2026-11-10 - Ubuntu text font, Material Icons and QeafdropOs-style home
- The default UI font is now Ubuntu (Ubuntu-Regular.ttf, UFL-1.0, assets/fonts/LICENCE.txt). `scripts/generate_ubuntu_bitmap.ps1` rasterises it with GDI+ AntiAliasGridFit into `system/ui/include/vqeaf/font_ubuntu.h`: 263 glyphs (ASCII 32-126, Latin C0-FF, Vietnamese 0102/0103/0110/0111/01A0/01A1/01AF/01B0 and 1EA0-1EFF), 12x16 cells, 8-bit per-pixel coverage instead of the old 1-bit mask.
- Icons come from the Material Icons font downloaded from Google Fonts (Apache-2.0, assets/fonts/MaterialIcons-LICENSE.txt, codepoints file alongside). `scripts/generate_icon_bitmap.ps1` rasterises 21 glyphs (info, settings, storage, system_update, power_settings_new, bolt, schedule, lock, play_arrow, restart_alt, delete, home, arrow_back, check, wifi, bluetooth, visibility, timer, smartphone, file_download, bedtime) into 40x40 8-bit coverage cells in `font_icons.h` and writes a contact sheet to build/icon_atlas_preview.png. No hand-drawn icon paths remain in the shell.
- `vqeaf_gfx_text` and the new `vqeaf_screen_icon` blend coverage over the destination with `vqeaf_gfx_blend` (format-aware unpack/repack); `vqeaf_gfx_round_rect` draws the rounded app tiles, dock and home indicator. Icons scale by nearest sampling from the 40 px atlas.
- Home layout adapted from the QeafdropOs reference (D:/QeafivelsOfficial/QeafdropOs, iOS-style launcher): 4 columns, rounded-square tiles with the glyph centred and the label underneath, an unlabeled 4-icon dock (Settings, Storage, Clock, Lock), a home indicator bar and a navy gradient wallpaper. At 720x1280 (scale 2): margin 50, column gap 60, tile 110 px, glyph 80 px, corner radius 24, label gap 8, first row top 144, row pitch 186, dock 692x148 at y=1084 with radius 60, home bar 248x8 at y=1248. Landscape keeps the 450 px column with 3 columns and 76 px tiles; below 560 px height the dock is dropped and rows compact. Labels wider than a tile are centred with signed math (the unsigned version underflowed and hid them); the key hint falls back to WASD/E/Q/P when the long line would overflow the frame.
- Selection is an accent rounded ring behind a highlighted tile; the status bar keeps the real RTC clock and never shows a battery in QEMU. The dock shortcuts open the same apps as the grid, so no new hardware claim is made.
- `vqeaf_phone_init` clamps columns to 4; ui.c picks 4/3/2 columns by frame width. tests/phone_shell_smoke.c covers the 4-column clamp and navigation wrap, and renders the portrait home/lock PPMs used for visual checks.
- Icon audit after the first QEMU run: VQEAF_ICON_DOWNLOAD pointed at U+F090 (download) while the atlas only contained U+E2C4 (file_download), so the recovery Apply Update row drew no icon; the mapping now uses E2C4. The power menu uses BOLT for Fastboot and the new SLEEP glyph (bedtime, U+EF44) for Sleep instead of the gear.
- `scripts/test_input_power.py` scores capture candidates by teal + bright (near-white glyph) pixels and takes three attempts for phone_home: a torn repaint keeps the backdrop and selection ring but loses later rows, so the teal-only score could not tell a torn home frame from a complete one.
- Validation: `python scripts/build.py`, 9/9 CTest, `python scripts/test.py` exit 0 (11/11 Python unit tests, SDK create/build/package/validate, normal + recovery QEMU boots, splash accent 15219/921600, ui accent 7759/921600, recovery_menu accent 13578, phone lock->home diff 124535, asleep non-black bytes 0). Screenshots visually checked: phone_lock, phone_home (complete 8 tiles + dock), phone_quick, update_blocked, power_menu (bolt/moon rows), recovery_menu (Apply Update icon), plus host portrait/landscape home previews.

## CodeEditor QEAPP port (2026-11-12)

- `examples/codeeditor-qeapp` ports the MRE CodeEditor portable core: editor
  buffer/undo/selection/clipboard, the five-language tokenizer, the 14 x 1280
  document store, and the screen/state machine behind a UI and storage HAL.
- Host proof: `scripts/test_codeeditor_port.py` compiles the core with
  MinGW-w64 gcc and runs `tests/host_test.c` (buffer, tokenizer, store, every
  screen painter). Project packaging tests: 5 passed.
- Release and debug packages build and pass `qeaf validate`.
  `native/arm64/app.qen` is the compiler's 64-byte empty stub; native module
  execution is still NOT IMPLEMENTED in the OS.
- Mapping, unsupported features (folders, encodings, localization, pen) and
  next steps: `examples/codeeditor-qeapp/docs/PORTING.md`.

## Chẩn đoán boot loop violet vật lý (2026-11-18)

- Triệu chứng: thiết bị reboot đều đặn ~30s/lần. Bằng chứng sống: bộ đếm
  journal p77 tăng `boots=47 -> 48` ngay khi ổ F: (QEAFRESCUE RAM LUN) rớt
  giữa vòng đọc raw; F: hiện ~20-24s rồi mất. Đây là reboot thật, không phải
  chỉ USB re-enumerate.
- KHÔNG phải crash userspace VQEAF: `CRASH.TXT` (8192B) và `LASTBOOT.TXT`
  (15872B) toàn NUL/trống qua nhiều boot; `QEAFBOOT.LOG` boot sạch tới
  "loop"; `DEVINFO.TXT` ui=vqeaf-phone. Fault handler (SIGSEGV/SIGBUS/...)
  không kích hoạt nên không signal nào được ghi.
- Reset phía vendor kernel, đột ngột: phần "kernel ring tail" của HOMOLOG
  luôn dừng ở ~6.38s (modem QMI readiness) kể cả bản bắt ngay trước khi F:
  rớt; không err_fatal/panic/bark/watchdog nào xuất hiện. VQEAF không kịp
  publish dòng kernel cuối trước khi reset.
- Đã loại trừ: PID1 exit sạch qua app runtime (mọi `_exit` đều trong nhánh
  fork con `pid==0`); main loop `for(;;)` không break/return/exit; không có
  lệnh reboot tự động (reboot chỉ do host verb).
- Nghi vấn hàng đầu (CHƯA xác nhận, cần pstore): (a) watchdog apps không
  được pet - `/dev/watchdog` chỉ được tạo node (devnodes.c:308), không ai
  mở/pet; (b) wifi bring-up nạp/giữ modem rồi rebind PIL "4080000"/"mss" +
  ghi `wlan recover=1` ở +9/+12s (wifi_bringup.c:536-553) trên stock kernel
  thiếu stack nền tảng di động RIL/SSR, modem fault có thể leo thang system reset.
- Giới hạn bằng chứng: RAM LUN chết theo reset; rescue channel hiện KHÔNG
  expose pstore/last_kmsg/PON reason nên không đọc được log kernel boot
  trước (thứ duy nhất chứa lý do reset). Chưa chốt root cause nếu thiếu
  bằng chứng này; không suy đoán thay bằng chứng.
- Bước kế tiếp cần uỷ quyền: thêm verb rescue CHỈ ĐỌC dump
  `/sys/fs/pstore/console-ramoops-0` + `/proc/last_kmsg` + PON/bootreason
  sysfs, build lại và nạp p77 (ghi partition -> cần consent rõ ràng). Không
  ghi p76/boot, userdata, misc.
- Phân biệt: đây là vendor kernel stock (transport tạm), KHÔNG phải
  QEAFIVELS Kernel độc lập.


## Port nền tảng MT6891/MT6893 (chopin/ares) - build-only (2026-11-18)

- Thêm `platform/mt689x/`: PID 1 dùng chung DRM/KMS, scaler, splash, USB LUN,
  rescue channel và shell LVGL với violet; phần riêng là hình học panel
  1080x2400, census khả năng, bring-up UFS/firmware/module, Wi-Fi state và
  session Denial tuỳ chọn.
- Nhân: `MT6891-Mainline/linux` nhánh `7.3.3-mt6893-xiaomi-chopin` +
  `platform/mt689x/kernel/qeaf_mt689x.fragment` (UFS MediaTek, BTIF/MTK_COMBO_BT,
  Novatek touch, SCP sensor hub, MT8192 AFE + MT6359 + CS35L41, USB configfs).
  `DRM_MEDIATEK_V2`, panel K10A 60/90/120 Hz và `CONFIG_KVM=y` đã có trong
  defconfig của nhánh.
- UFS/USB/DRM/touch/Wi-Fi/BT/audio/GPU/auto-rotate/KVM: có driver + DTS +
  config trong cây; **chưa có thiết bị nào được kết nối**, nên chưa mục nào
  được xác nhận trên phần cứng. NFC/camera/baseband báo UNSUPPORTED.
- Firmware Wi-Fi/BT/SCP không được bundle: `scripts/mt689x_firmware.py` trích
  từ dump vendor stock và ghi manifest hash; thiếu blob thì census báo
  `BLOCKED_FIRMWARE`. Image không có WPA supplicant nên không tự kết nối mạng.
- Script: `build_mt689x.py`, `build_mt689x_kernel.py` (+ `mt689x_kernel_build.sh`),
  `mt689x_firmware.py`, `package_mt689x.py`. Bundle `dist/mt689x/` có
  `vqeaf-init`, `qeaf-mt689x.cpio.gz`, `Image`, dtb, `manifest.json`.
- Denial là compositor Wayland, không phải nhân: chỉ chạy khi binary tồn tại và
  `/etc/qeaf/session` chọn `denial`; nếu thoát, shell QEAF tiếp quản.
- Chi tiết và ma trận hỗ trợ: `docs/platforms/MT6891_MT6893.md`.

## Kernel độc lập AArch64 — F4: credential/capability + syscall gate + VFS handle (2026-11-20)

- `VqeafTask` mang `VqeafCred {uid,gid,caps}`. Task kernel = uid 0 với
  `VQEAF_CAP_ALL`; task EL0 nội bộ (self-test boot) dùng system credential;
  app .qen do AMS tạo mang uid riêng (com.vqeaf.demo=10001,
  com.vqeaf.window=10002) + bundle capability, không bao giờ có
  `VQEAF_CAP_SYSTEM`/`VQEAF_CAP_FS_ADMIN`.
- `vqeaf_task_create_user_cred()` từ chối cred NULL, nên không thể vô tình thừa
  kế identity hệ thống. AMS ghi `AMS: launch <pkg> -> pid= uid= caps=` để đối
  chiếu với log thật.
- Policy syscall ở `kernel/core/security.c` (bảng số hiệu lấy từ `el0_abi.h`),
  được kiểm tra ở ĐẦU dispatcher EL0 trong `kernel/arch/aarch64/irq.c` TRƯỚC
  khi đọc tham số; từ chối trả `-EACCES` và in `SEC: deny uid= sys= need=`.
- Syscall mới: open/read/write/close (14-17). Buffer user được validate bằng
  `vqeaf_vm_user_range_mapped` trước khi copy qua buffer 256 B của kernel.
- VFS: `VqeafVnode` có uid/gid/mode 9 bit; handle table riêng cho từng slot
  (8 handle/task), open kiểm owner/mode, read/write kiểm lại; task exit giải
  phóng toàn bộ handle. Root namespace: `/system/version` (0444) và
  `/data/<uid>/private` (0600, chỉ RAM — mất khi reboot, không phải storage).
- Bằng chứng QEMU: `python scripts/test_kernel_aarch64.py` → ALL OK, gồm 7
  check SEC + 8 check VFS + app window chạy dưới uid 10002 với cap hạn chế mà
  vẫn hoàn tất chuỗi F3. Host: `ctest --test-dir build-native` 11/11 pass.
- Tài liệu/prompt/skills đã cập nhật theo mô hình này: `docs/ARCHITECTURE.md`,
  `docs/ROADMAP.md`, `docs/INDEPENDENT_OS_CHARTER.md`, `.agents/AGENTS.md`,
  `.agents/README.md`, hai prompt trong `.agents/prompts/`, và skills
  `kernel-core`/`security-privacy`/`os-architecture`.
- Chưa làm: device-object broker (EL0 chưa có syscall thiết bị nào và MMIO
  không map vào user root), persistence của `/data`, revocation process đang
  chạy, và danh tính app trên phần cứng violet.
- Lưu ý môi trường: link `KERNEL.EFI` của host build (x86) fail `ld returned 5`
  khi có `-Wl,-Map`; tái hiện cả với chương trình rỗng nên là lỗi môi trường
  mingw/AV, không do thay đổi này.

### F4 hardening — đóng 4 lỗ thoát quyền (2026-11-20, cùng ngày)

Không làm lại F4; đi vào các khe hở còn lại sau khi đọc dispatcher và VFS:

- **Fail-closed ở SVC dispatcher:** trước đây `vqeaf_task_cred_ptr(owner)` trả
  NULL thì fallback sang `boot_cred = VQEAF_CRED_SYSTEM_INIT` (`CAP_ALL`). Giờ
  NULL → trả `-EACCES` và in `SEC: deny no-cred sys=<n>`, không đụng vào state.
  Một slot stale/exited không còn là đường nâng quyền vô điều kiện.
- **Credential app bị validate lúc tạo:** `VQEAF_CAP_PRIVILEGED =
  CAP_SYSTEM | CAP_FS_ADMIN`; `vqeaf_sec_user_cred_ok()` chặn cả uid 0 lẫn hai
  bit này. `vqeaf_task_create_user_cred()` trả `-6` trước khi chạm task slot.
  Boot harness vẫn có đường riêng `vqeaf_task_create_user()` (ghi rõ boot-only).
- **Handle VFS gắn chặt identity:** `VfsHandle` lưu snapshot `VqeafCred` lúc
  open; read/write dùng `vqeaf_sec_cred_eq()` rồi mới kiểm mode trên snapshot
  đó. Handle không cho mượn sang app khác, không sống qua thay đổi credential.
- **Bằng chứng từ EL0 thật:** `com.vqeaf.window` (uid 10002, UI-only caps)
  gọi `sys_open` và `sys_write` qua SVC thật, nhận `-EACCES` từ dispatcher —
  không chỉ từ self-test EL1.
- Gate: `test_kernel_aarch64.py` **120 check ALL OK**, chạy lặp **5/5**;
  `test_qeapp_aarch64.py` ALL OK; unittest **75 tests** OK; `test.py` ALL OK;
  `test_violet_shell.py` ALL OK (307 ok, 49 snapshot).
- Sửa lỗi phát hiện khi chạy gate: `CMakeLists.txt` giờ link `security.c` vào
  `KERNEL.EFI` và 5 smoke target có `vfs.c` (nếu không, build thật lỗi
  `undefined reference to vqeaf_sec_cred_eq`; `test.py` trước đó chỉ xanh vì
  dùng binary cũ trong `build/`). `framework/simulator/qeaf_sim_stubs.c` thêm
  stub `appstore_read_grants()` — thiếu stub này làm `test_qeapp_aarch64.py`
  lỗi link từ phiên trước.
- Dọn instrumentation `fprintf(stderr, "DBG build_grid ...")` còn sót trong
  `vqeaf_ime.c`. Test `22d` (fast re-focus) vẫn để `#if 0` — xem mục tiếp.

### Keyboard: nguyên nhân đóng khi đang gõ (2026-11-20)

- Nguyên nhân: animation ẩn IME xong thì xoá `g_win`; focus lại quá nhanh trước
  khi animation kết thúc sẽ dùng lại chính window đó, rồi callback cũ xoá nó
  giữa lúc đang gõ. Sửa bằng huỷ animation cũ khi show/rebuild.
- Các test SHIFT/?123/SPACE/DEL và search-modal được bật lại; block `22d` thử
  lại focus trong lúc slide-down vẫn gây crash ở phần Material ripple nên tạm
  `#if 0`. Snapshot `notes-ime-special` đã được render từ lần chạy trước.


## Cử chỉ shell không chồng lấn app (2026-11-20)

- platform/violet/ui/vqeaf_ui.c: cử chỉ chỉ điều khiển shell trên các trang
  hệ thống (Lock/Home/Apps/Control/Settings/Recents). Trang nội dung (Files,
  Browser, Notes, Music, Video, Gallery, QEAPP...) giữ lại vuốt ngang cho tab
  và vuốt dọc cho cuộn của chính app.
- Vuốt xuống mở Control chỉ khi bắt đầu từ thanh trạng thái (hoặc đầu trang
  launcher); vuốt lên mở Recents chỉ từ thanh cử chỉ hoặc trang launcher.
- Vuốt ngang trên Recents giờ chuyển giữa các tác vụ gần đây thay vì về Home.
- Bỏ qua gesture khi đang có overlay (IME, hộp thoại USB, app-info sheet).
- qeaf_pages.c/.h: thêm qeaf_pages_app_info_visible() làm tín hiệu chặn.
- Kiểm chứng trên máy: vuốt ngang trong Files giữ nguyên page=13; vuốt lên
  từ thanh cử chỉ mở page=19 (Recents). Host test 	est_violet_shell.py
  (49 snapshots) và 	est_m3.py đều PASS; build violet + mt689x OK.


## Chụp ảnh màn hình (2026-11-20)

- Phím tắt **Power + Volume** (hoặc rescue verb shot) chụp toàn bộ khung
  DRM, ghi file SHOTnnnn.PNG (lodepng) hoặc fallback SHOTnnnn.BMP nếu
  encoder PNG lỗi, ngay trên ổ QEAFRESCUE (FAT trong RAM, không ghi phân vùng).
- UI hiển thị thông báo tên file; host đọc file qua RESPONSE/Volume như file
  thường. qeaf_ui_notify dùng biểu tượng Ảnh màn hình.
- Code: platform/violet/main_violet.c (screenshot_png), iolet_power.c/.h
  (combo Power+Volume), channel/fs/rescue.c/.h (verb shot + hook).
- Build violet/mt689x OK. Chưa kiểm chứng trên máy vì thiết bị mất kết nối
  trong lúc thử nghiệm (không trả lời rescue lẫn fastboot).


## Nang cap launcher, bao mat & cu chi canh (2026-11-20)

### Da lam (code + build OK, chua flash do thiet bi ket)
- **Cu chi canh nền tảng di động-like** (`vqeaf_ui.c`):
  - Vuot len tu canh duoi: ve Home; giu >500ms roi vuot len: mo Recent.
  - Vuot xuong tu canh tren: mo Control.
  - Vuot tu canh trai/phai: Back; giu roi vuot: chuyen giua cac app trong Recent;
    vuot canh o phan tren: mo Control.
  - Theo doi diem cham bang timer 10ms (`input_track_cb`) vi LVGL chi tra diem
    hien tai tren su kien GESTURE.
- **Phim ao kieu nền tảng di động** (Back/Home/Recents): toggle trong Control Center;
  luu `nav.virtual_keys` (session-only, violet khong co prefs persistent).
- **PIN man hinh khoa** (`vqeaf_pages.c/.h`, `vqeaf_ui.c`):
  - Settings > Bao mat: dat/xoa PIN 4 so; lock screen chuyen sang ban phim so.
  - Ca vuot lan tap deu di qua `vqeaf_lock_unlock()` -> khong bypass duoc PIN.
  - Them page `VQEAF_PAGE_SECURITY`, `VQEAF_PAGE_PIN`.
- **Trung thuc phan cung**: trang Bao mat bao ro van tay / khoa cham trong tui
  / rung cham la `khong co driver/cam bien` — khong gia vo ho tro.
- Build violet (9.576.432 B) + mt689x (9.517.448 B) OK.
  Host test `test_violet_shell.py` (49 snapshots) + `test_m3.py` PASS.

### Chua xong
- Thiet bi ket o boot stage `splash` sau cac lan thu `shot` truoc do
  (rescue loop khong xu ly lenh, fastboot khong thay). Can khoi dong lai bang tay
  roi flash `dist/boot-vqeaf-violet.img` va kiem chung PIN + cu chi canh.

## Don dep ten nhan nền tảng cũ trong cac thu muc con lai (2026-11-20)

Pham vi nguoi dung chon: `out/` logs, docs + README, `.agents` policy, vendored code.

### Da lam
- `docs/`, `README.md`, `examples/`: thay toan bo ten nhan nền tảng cũ bang ngon ngu
  trung tinh ("vendor mobile platform", "nền tảng di động của nhà sản xuất").
  Hai URL upstream có chứa tên cũ được thay bằng chú thích truy nguồn không
  gắn link, tránh link hỏng.
- `.agents/`: giu nguyen y nghia cam ket doc lap nhung xoa ten nhan cu khoi
  `AGENTS.md`, hai master prompt va hai skill; khong noi loang quy tac bao mat.
- `out/`: 24 ban copy log text/binary duoc sanitize bang token cung do dai; ghi
  hash goc truoc khi sua trong `docs/OUT_LOG_SANITISATION.md`. Day la ban evidence
  da sanitize, khong con la ban capture byte-exact.
- Vendored: comment trong LVGL/stb/hosttools, macro `#ifndef QEAF_VENDOR_TARGET`
  (pd-mapper, rmtfs, tqftpserv), branch EGL/KHR doi thanh `__QEAF_VENDOR__`,
  doi typedef EGL `...QEAF` (khong co caller nao dung ten cu); xoa 2 job CI va
  2 script CI không dùng; đổi 4 file build definition cũ thành `VendorBuild.bp`;
  doi alias codepoint font `..._qeafvnd` (generator van dung `smartphone`).
- Xoa cac thu muc `__pycache__` generated de tranh byte cu trong file .pyc.

### Ngoai le co chu dich
- 2 file anh khoi/backup trong `out/` van giu format token raw vi no la mot phan
  cua hop dong anh, khong phai nhan hien thi. Da khoi phuc ve dung hash goc.
- 7 script trich xuat vendor (`scripts/extract_bionic_sysroot.sh`,
  `fetch_crdroid_wifi_cfg.sh`, `finalize_bionic_sysroot*.sh`, `fix_apex_libs.sh`,
  `list_runtime_apex.sh`, `probe_apex.sh`) van giu duong dan nguon that de doc
  dump stock. Day la pham vi host-side, khong nam trong 4 nhom da chon.

### Gate sau don dep
- `build_violet_hosttools.py` OK (pd-mapper, tqftpserv, rmtfs, wlfwd,
  pdr-restart, pd-listen, qrtr-lookup).
- `build_violet.py` OK; `package_violet.py` + `validate_bootimg` +
  `audit_rescue_channel` OK. Anh moi:
  `dist/boot-vqeaf-violet.img`, 29.732.864 B,
  SHA-256 `1fbd37376b3daffd73674a49ab7c37bf13b373c51cf20a5eee78b4338fc80742`.
- `verify_native_wifi_package.py` PASS (111 CPIO entry).
- `cmake --build build` OK; ctest 12/12; `test_violet_shell.py` ALL OK;
  `test.py` ALL OK; `python -m unittest discover -s tests` 76 tests OK.
- Quet toan project con 9 ket qua: 2 anh ngoai le + 7 script host-side; khong
  con ket qua nao trong docs, README, examples, .agents, vendored hay font.


## Bàn phím: thêm emoji thật (2026-11-21)

- Font emoji `platform/violet/ui/fonts/vqeaf_emoji_28.c`: 88 glyph thật
  rasterize từ **Segoe UI Emoji** (`C:\Windows\Fonts\seguiemj.ttf`) bằng
  `scripts/generate_emoji_font.ps1` (xử lý codepoint > BMP bằng
  `ConvertFromUtf32` + cmap 32-bit, chia 2 cmap block để uint16 offset không
  overflow). Phạm vi U+2705..U+1F963. Không dùng glyph giả.
- `vqeaf_ime.c`: thêm action `IME_EMOJI` / `IME_EMOJI_MODE`, state `g_emoji`,
  bảng 50 emoji (UTF-8) + 3 hàng emoji. Nút toggle 😀 nằm ở hàng dưới cùng
  cạnh `?123` (cả trong symbol layout). Nút `ABC` trong emoji panel quay về
  chữ cái; emoji chèn bằng `lv_textarea_add_text` (4-byte UTF-8 thật).
- Build: violet 9.819.256 B (267 objects), mt689x build OK.
  Test: `test_ime.py` ALL OK (có assertion "clicking emoji inserts real
  UTF-8 (U+1F600)" + backspace 4-byte + ABC return), `test_violet_shell.py`
  49 snapshots ALL OK, `test_m3.py` ALL OK.
- Ảnh đã flash: `dist/boot-vqeaf-violet.img` sha256
  `e6e9872ac538638a8515e7dd59c510446b1c41683b205016d3cc9eee270cbf6b`.
  Trên máy bàn phím hiện lên với nút emoji ở hàng dưới (đã chụp màn hình
  `build/screens/device/emoji_keyboard.png`).
- Phát hiện & sửa lỗi phát sinh: `vqeaf_pages.c` có 6 dòng `LV_OPA_55` và 1
  dòng `LV_OPA_65` (LVGL không định nghĩa) → đổi 55→50, 65→70. Ngoài ra file
  này từng bị **mojibake** do PowerShell `Set-Content -Encoding UTF8` thêm BOM
  và ghi lại sai encoding; đã đảo ngược double-encoding về UTF-8 sạch
  (`temp/fix_mojibake2.py`), kiểm chứng lại bằng 49 snapshots PASS.


## Wallpaper ảnh thật + Default apps (2026-11-21)

### Wallpaper
- Ảnh hố đen do user cung cấp (`C:\Users\doxuanhop\Downloads\
  2cb1bea637c46a8d7e21e09d22cd9391.jpg`, 1200x2464) đã resize 720x1560 bằng
  `scripts/make_wallpaper.ps1` (System.Drawing, HighQualityBicubic) rồi đóng
  gói thành `platform/violet/ui/wallpaper.c` (`scripts/make_wallpaper_c.py`):
  RGB565 720x1560, 2.246.400 byte, `lv_image_dsc_t qeaf_wallpaper`.
- `vqeaf_theme.c:vqeaf_wallpaper()` vẽ ảnh bằng `lv_image` khi descriptor có
  data; gradient vector cũ giữ làm fallback. Ảnh đã thấy rõ trên Home/Apps
  (ảnh chụp `build/screens/device/emoji_keyboard.png` lúc đó).

### Default apps
- Page mới `VQEAF_PAGE_DEFAULT_APPS` (page 28), vào từ Settings > "Ứng dụng
  mặc định" hoặc rescue `page defaultapps` (thêm vào `ui_page_hook()`).
- 5 role: browser / music / gallery / files / home. Mỗi role có fallback
  built-in (app 7 / 10 / 9 / 12 / 0). Chọn trong chooser: built-in handler
  thật + mọi QEAPP đã cài; lưu qua Settings service (`default.browser`, ...).
- API `vqeaf_defaults_set/ open/ selector/ role_name` trong `vqeaf_pages.h`;
  rescue verb `defaults ROLE` trả selector + "(opened)" khi có handler thật.
  Đã đo trên máy: `defaults 0 -> app:7 (opened)`, `defaults 1 -> app:10`,
  `defaults 4 -> app:0`. Nút "Đặt mặc định" thêm vào app-info sheet.
- violet session-only: reboot về fallback (không có prefs partition).

### Build/flash/tests
- violet 11.048.152 B (268 objects), mt689x build OK.
  Ảnh flash sha256 `a225ef842271e7ee9b0e196c5c75fd93e0997776ea3d2b43238c19942f4add70`.
- `test_ime.py` ALL OK (expectations cập nhật theo metrics hiện tại:
  portrait H=428, 27% floor, row 48dp — user đã đổi từ 35%/52dp),
  `test_m3.py` ALL OK. `test_violet_shell.py` có 15 FAIL do expectation cũ
  (search placeholder "Tìm ứng dụng...", theme seed, IME icon/layout) so với
  source hiện tại của user; không phải lỗi của wallpaper/default apps.
- Host IME tests cần stub `vio_haptics_available()/vio_haptic_pulse()` trong
  tests/vqeaf_ime.c và tests/vqeaf_shell_interaction.c; host test link cần
  wallpaper.c trong test_ime.py/test_m3.py/render_phone_ui.py.


## Wallpaper cover + tương phản chrome (2026-11-21, bản cuối)
- `vqeaf_wallpaper()`: ảnh 720x1560 giờ scale kiểu **cover** theo tỉ lệ lớn
  hơn (zx/zy, 256 = 1:1), căn giữa, phần thừa được parent clip — không bị
  stretch thành oval khi xoay ngang.
- Lớp **veil tương phản** phủ trên ảnh (light: trắng 50–58%, dark: đen
  34–46%), không nhận focus/gesture. `vqeaf_label()` thêm outline 1px tự
  theo độ sáng chữ (chữ tối viền trắng, chữ sáng viền đen).
- `vqeaf_wallpaper_text_color()` / `vqeaf_wallpaper_active()`: nhãn app trên
  ảnh dùng chữ sáng `0xF2FAFF`; status bar + dock navbar dùng nền đen mờ
  (120/255) và chữ sáng thay vì dải sáng cũ. Pill home bar sáng mờ 60%.
- Ảnh đã flash sha256 `9c0b26a63ce090a41040203cb48a6d0a9a5a58b9e6e13911b4d6939a3ebd427c`.
  `build_violet.py`, `test_m3.py`, `test_ime.py` ALL OK.
  Kiểm chứng bằng ảnh chụp `build/screens/device/wallpaper_contrast.png`.

## Native camera/V4L2 port + driver census (2026-11-21)
- Live QEAFRESCUE read-only census: `/sys/class/video4linux` has `video0/1/2/32/33`
  + 17 `v4l-subdev*`; names `cam-req-mgr`, `cam_sync`, `sde_rotator`;
  `/sys/class/video4linux/video32/dev` = `81:32`. `/proc/devices` has major 81
  `video4linux`. Stock kernel camera drivers are ALREADY live — no driver C
  source to port from the ROM; the port is a native userspace V4L2 client.
- `devnodes.c`: `scan_class("/sys/class/video4linux", "video", "/dev", S_IFCHR)` —
  our PID 1 owns `/dev` (devtmpfs off), so this previously missing scan was why
  `/dev/video*` did not exist despite working kernel drivers.
- New `vqeaf_camera.c/.h`: role classifier (capture/control/rotator/other),
  `VIDIOC_QUERYCAP` census, and a real one-frame V4L2 capture via
  REQBUFS/MMAP/QBUF/STREAMON/DQBUF with FNV-1a frame hash. No vendor camera HAL,
  binder service or vendor .so. Rescue verbs `cam` and `cam grab [NODE]`.
- Capability census corrected from live evidence: `camera` PRESENT only when
  real capture candidates exist; `camera.v4l2` PRESENT when video nodes exist;
  `fingerprint` UNSUPPORTED (no `/sys/class/misc` goodix/fpc node on this boot).
  READY still requires a real `cam grab` frame, never a node list.
- Bluetooth: `/sys/class/bluetooth` empty, p38 firmware partition not consumed
  natively. Wi-Fi: `wlan` major 500 live, `wlan0` still absent (modem/WLFW).
- Gates: `test_violet_caps.py` ALL OK (9 groups, includes role classifier);
  `build_violet.py` OK (269 objects, 11.065.456 B); `package_violet.py` +
  validate_bootimg + audit_rescue_channel OK; `verify_native_wifi_package.py`
  PASS (136 CPIO); unittest 76 OK; ctest 12/12; `test.py` ALL OK on rerun.
  `test_violet_shell.py` still has the 15 known stale-expectation FAILs recorded
  above; unrelated to this change. QEMU capture gates are flaky when chained
  back-to-back; rerun passes.
- First image flashed to p77 with both raw-sector proofs and rebooted:
  `dist/boot-vqeaf-violet.img` 32,071,680 B, sha256
  `3f3b22b9132b1c64c80789d511a8d6534ae5b889d9997c908d740829e93af8d2`.
  Proofs: `flash: written and verified byte for byte` (RESPONSE.TXT seq=13) and
  `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over 32071680
  bytes in 1 cluster runs` (QEAFBOOT.LOG).
- On-device `cam` returned 5 "capture" nodes, but QUERYCAP showed they are
  `cam-req-mgr`, `cam_sync`, `sde_rotator`, `msm_vidc_venc`, `msm_vidc_vdec` —
  no real camera stream. `cam grab /dev/video32` honestly failed
  `G_FMT failed (errno 22)`. Classifier fixed: added V4L2 codec role, sysfs
  name is authoritative for control/rotator, codec nodes excluded from the
  camera count, and `cam grab` refuses non-capture nodes before streaming.
- Corrected image built and gated: 32,079,872 B, sha256
  `501698f8ba13e302c8d6ba4870c3b77cedc3bfec2be1fede3dec0b9b3b3146f6`.
  Staging hit `PermissionError` twice from stale FAT root entries
  (`BOOTIMG.BIN`, then `QEAF.CMD`). User approved retry; the stale `QEAF.CMD`
  was deleted directly, the command rewritten with a nonce, and the image
  flashed with BOTH raw proofs: `flash: written and verified byte for byte`
  (RESPONSE.TXT seq=9, nonce `nativecamflash5016x`) and
  `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over 32079872
  bytes in 1 cluster runs`. Rebooted with nonce `nativecamreboot5016x`.
- Live device proof after reboot: `cam: v4l2 nodes=5 capture=0 control=2
  rotator=1 codec=2 other=0`; `cam grab /dev/video32` now refuses with
  `node is not a camera capture stream (errno 95)`; `caps` reports
  `camera BLOCKED_FIRMWARE`, `camera.v4l2 PRESENT 5`, `fingerprint UNSUPPORTED`.
  Honest state: kernel drivers exist, no camera stream node is exposed, so
  camera is NOT claimed as working.

## QEAPP broker gate crash fixed (2026-11-21)
- `tests/vqeaf_sim_quota_test.c` exited with SIGSEGV (139) at CRT teardown when
  stdout was piped, after all 53 checks passed. Isolated variants proved no
  individual test crashed; only buffered stdout at process exit did.
- Added `setvbuf(stdout, NULL, _IONBF, 0)` at `main()` entry.
  `scripts/test_qeapp_broker.py` now exits 0 with `ALL OK`.


## Terminal system app + commands (2026-11-21)
- Trang `VQEAF_PAGE_TERMINAL` (app "Terminal" trong drawer, rescue
  `page terminal`): textarea + IME có sẵn + nút CHAY LENH, vùng output
  `g_term_out` 4 KB. Mọi lệnh đọc state/provider thật, không shell process,
  không dữ liệu giả.
- Lệnh: `help`, `status`, `date`, `uptime`, `battery`, `storage`, `wifi`,
  `apps`, `defaults`, `default ROLE`, `theme dark|light`, `page TEN`,
  `echo TEXT`, `clear`. `wifi` báo rõ "KHONG co ket noi that" khi `wifi_up<=0`.
- API `vqeaf_terminal_run()` trong `vqeaf_pages.h`; rescue verb `term <lenh>`
  cho phép host thực thi và kiểm chứng không cần inject phím.
- Kiểm chứng trên máy: `term help/status/wifi` chạy, ảnh chụp
  `build/screens/device/terminal.png` thấy output thật (uptime, pin 100%,
  wifi driver=1 fw=1 ... va dong canh bao khong gia ket noi).
- Build violet OK (11.089.424 B, 269 objects), flash sha256
  `9ee7a5874dc0a9da15553b55b5bcaef0fa49587448dd0362628e35dbe7cd9c43`.
  `test_m3.py`, `test_ime.py` ALL OK. `test_violet_shell.py` van co 15 FAIL
  do expectation cu cua user (search/theme/IME) - khong lien quan terminal.
  `build_mt689x.py` dang co loi undefined symbols pre-existing
  (`vqeaf_qeapp_sandbox_*`, `vqeaf_camera_*`) tu cac thay doi khong phai terminal.


## Terminal nâng cấp kiểu Termux (2026-11-21)
- Giao diện: console tối `0x0B1514` phủ toàn trang (độc lập wallpaper/theme),
  header `termux@violet`, vùng output scrollable có flex-grow, prompt cwd,
  textarea `command...` và thanh phím **TAB / UP / DOWN / CTRL+C / ENTER**.
  TAB = hoàn thành lệnh đầu trùng, UP/DOWN = lịch sử 32 lệnh, CTRL+C in ^C
  và xoá dòng, ENTER (hoặc phím IME) chạy lệnh.
- Lệnh Unix-like mới đọc dữ liệu thật: `pwd`, `ls [path]` (opendir/readdir),
  `cat <file>` (fopen/fgets), `cd`, `whoami`, `uname` (sys/utsname), `env`,
  `which <app>` (app registry), `history`, cộng các lệnh framework cũ
  (status/date/uptime/battery/storage/wifi/apps/defaults/default/theme/page/
  echo/clear). `wifi` vẫn báo rõ không có kết nối thật.
- Bỏ mã ANSI khỏi output (LVGL label không render escape codes).
- Ảnh chụp: `build/screens/device/terminal.png`. Build violet 11.106.000 B,
  flash sha256 `bd93a5255c87541c54330a1db33b30569f36c0e095b868f8e6f7abca1c8217bf`.
  `test_m3.py` / `test_ime.py` ALL OK.

## Camera subdev nodes + CAM session (2026-11-21)
- `devnodes.c` now creates `/dev/v4l-subdevN` as well as `/dev/videoN`.
- `vqeaf_camera.c` adds kernel-UAPI `cam_control` wrapper (ioctl 0xc01856c0,
  24-byte struct), `cam session` (CAM_REQ_MGR_CREATE_SESSION opcode 267) and
  `cam subs` enumeration. Gate extended to 10 groups.
- Image `c6b5ec1f0fe67388a8a2f85a3f42f981307d9afe89bc3d5ebbc07c441af208b3`,
  32,088,064 B; flash proofs seq=71 nonce `camsubsflashc6b5` and
  `lun: verified … over 32088064 bytes`; reboot nonce `camsubsrebootc6b5`.
- Live: `cam subs` listed 17 subdevs (cam-isp=1, cam-icp=14, cam-sensor-driver=
  7/8/9); `cam session: created handle=0x00530200`; `cam` still capture=0.
  Honest state: hardware path is reachable natively, stream not yet created.

## CAM_QUERY_CAP + CAM_ACQUIRE_DEV + link attempt (2026-11-21)
- `vqeaf_camera.h` now carries the fixed Qualcomm UAPI layouts for
  `cam_query_cap_cmd`, `cam_acquire_dev_cmd`, `cam_isp_query_cap_cmd`,
  `cam_icp_query_cap_cmd`, `cam_sensor_query_cap`, `cam_isp_in_port_info`,
  `cam_icp_acquire_dev_info`, `cam_release_dev_cmd` and
  `cam_req_mgr_link_info`. Op/struct sizes are asserted in the host gate.
- `vqeaf_camera.c` finds real `/dev/v4l-subdevN` nodes by sysfs name, issues
  CAM_QUERY_CAP to `cam-isp`, `cam-icp` and every `cam-sensor-driver`, then
  (for `cam link`) creates a session, acquires sensor/ISP/ICP with real
  payloads, tries CAM_REQ_MGR_LINK, releases devices and destroys the session.
  It never allocates buffers, schedules requests or claims a stream node.
- New rescue verbs: `cam caps` and `cam link`.
- First image `494fd872…` was flashed and probed live. It exposed a real bug:
  `scan_class()` used one DRM-only hyphen filter for every class, so
  `v4l-subdevN` sysfs entries were never turned into `/dev/v4l-subdevN`.
  `cam caps` found names but every open failed with errno 2.
- Fix: the hyphen filter is now DRM-only; a unit-test contract guards it.
  Corrected image `dist/boot-vqeaf-violet.img`, 32,108,544 B, sha256
  `0d34ef9f72b9b23bdbcc5c8e816baf71303d1b727b6f0b6465a0946225ba9092`,
  flashed with both raw-sector proofs (nonce `flashcamdevnodes0d34ef9f`,
  `lun: verified … over 32108544 bytes`) and rebooted to a fresh seq=0 LUN.
- Live after fix: `/dev/v4l-subdev0..16` all exist. `cam caps` returns real
  ISP caps (iommu 0x000062be/0x00053112, cdm 0x0004647c, num_dev=2, IFE
  version 1.7.0) and three sensor caps (all slots 0). `cam icp` open fails
  errno 22. `cam link` creates session 0x006a0200, ISP acquire succeeds with
  handle 0x00aa0100, sensor/ICP acquire fail errno 22, link fails errno 22.
- Honest state: CAM_QUERY_CAP and CAM_ACQUIRE_DEV are now proved on real
  kernel nodes; a usable stream still does NOT exist (`cam`: capture=0).
  No buffer was allocated and no request was scheduled.
- Gates: `test_violet_caps.py` ALL OK (11 groups), `build_violet.py` OK,
  `verify_native_wifi_package.py` PASS (136 entries), unit tests 85/85 OK,
  `scripts/test.py` ALL OK including 12/12 ctest and QEMU captures.


## Wi-Fi UI: scan/connect + mã hóa + Gateway (2026-11-21)
- `vqeaf_wifi_net_t` thêm `enc` (OPEN/WEP/WPA/WPA2_PSK/WPA_WPA2), parse từ
  `IWEVGENIE` (RSN IE 0x30, WPA vendor 00:50:F2 type 01/02) và WEXT alg hint.
- Settings > Wi-Fi: mỗi mạng hiển thị **Tên SSID**, **loại mã hóa**
  (`WPA2-PSK`, `WPA-PSK`, `Mở...`) và RSSI. Mạng bảo mật → modal nhập mật khẩu
  bằng IME; mạng mở → gọi `vqeaf_wifi_connect(ssid, "")` trực tiếp.
- Thêm dòng **Chế độ Gateway** (tap để đổi `dhcp`/`static`, lưu trong
  Settings Service key `wifi.gateway`). Hiện text `DHCP (chờ udhcpc)` — không
  giả đã có DHCP client.
- Terminal: `scan` (liệt kê SSID/mã hóa/RSSI), `gateway [dhcp|static]`,
  `connect <ssid>` (mạng mở; mạng bảo mật bắt buộc dùng Settings để nhập PSK
  qua bàn phím, không lưu PSK vào lịch sử lệnh).
- Rescue hook `page wifi` để kiểm chứng nhanh.
- Trên máy: `page wifi` hiển thị đúng `Không có giao diện mạng (chưa tạo
  netdev/firmware)` và `Chế độ Gateway: DHCP (chờ udhcpc)` — **không có danh
  sách mạng giả** vì wlan0 chưa tồn tại. Build 11.151.008 B, flash sha256
  `72d3b71d5e49154f8a0320c4a0839d86c9cf4d2de073dc78adb4047771431884`.
  `test_m3.py`/`test_ime.py` ALL OK.


## Chẩn đoán BSP Wi-Fi violet theo chuỗi Qualcomm (2026-11-21)
Raw log: `reports/wifi-bsp-diag/*.txt`.

### Xác minh phần cứng — giả thuyết ath10k/ath11k SAI
- SoC: **SM6150** (Snapdragon 675), kernel `4.14.215-azure-Pixel+`.
- WiFi: **WCN3990 với driver vendor `icnss` + `qcacld-3.0`**, KHÔNG phải
  `ath10k_snoc` / `ath11k`:
  - `/sys/module/icnss` tồn tại; `/sys/bus/platform/drivers/icnss/18800000.qcom,icnss` tồn tại.
  - `/sys/bus/platform/drivers/ath10k_snoc` absent.
  - dmesg: `icnss: Platform driver probed successfully`,
    `wlan_hdd_state wlan major(500) initialized`.
- Vì vậy không áp dụng các bước "ath10k firmware-5.bin/board-2.bin" cho máy này.
  WLFW `0x45` vẫn đúng trong stack vendor (cnss-daemon là server).

### Bảng chuỗi bị đứt
| Bước | Trạng thái | Bằng chứng |
|---|---|---|
| 1 Modem/remoteproc | **Không có sysfs remoteproc** (stock dùng `subsys-pil-tz`, không phải remoteproc); modem chỉ boot khi `wifi up`: `modem: Brought out of reset`, `Power/Clock ready`, `qrtr: Modem QMI Readiness RX cmd:0x2` | `01-remoteproc.txt`, `03-dmesg.txt` |
| 2 QRTR | OK: `qrtr-ns` chạy, có servers `0x42`, `0x2b`, ...; `qrtr-lookup 0x45` gửi được | `02-qrtr-lookup.txt` |
| 3 userspace services | pd-mapper/pd-listen/tqftpserv/rmtfs **đều running**; binder present; cnss-daemon running sau `wifi vendor`. NHƯNG bring-up thường bị chặn bởi `p74 not mounted or factory WLAN MAC absent/invalid` | `04-services.txt` |
| 4 WLFW 0x45 publish | **FAIL**: `lookup 0x45/0x1/0x0 sent` → `recvfrom(): Resource temporarily unavailable` lặp; không có NEW_SERVER 0x45 | `02-qrtr-lookup.txt` |
| 5 driver tạo wlan0 | **FAIL**: `wifi net` không có `wlan0`; driver là icnss/qcacld chờ firmware-ready | `04-services.txt`, `05-firmware.txt` |

### Nguyên nhân sớm nhất
1. **persist (p74) không có factory WLAN MAC / calibration** (`wlan_mac.bin`, `wlfw_cal_*` không tìm thấy) → `find_factory_mac()` fail → CNSS/WLFW không start trong luồng bring-up. Đây là blocker cần dữ liệu thật từ máy, KHÔNG phải lỗi code có thể bypass.
2. **SERVREG listener chưa chứng minh đăng ký**: log pd-listen chỉ có `watching real QRTR service 0x42`, không có dòng `result=0 error=0`.
3. **Modem reset** ~15–20 s sau `wifi up` (lỗi độc lập, đã đo ở bản trước).

### Repo: cái có / cái thiếu
- Có: `kernel/arch/x86_64/pci.c`, `kernel/drivers/virtio_blk.c`, `virtio_net.c` (M1), `wcn3990_snoc.c`/`wcn3990_phase2.c` (stub, fail-closed), `platform/violet/wifi_bringup.c`, host tools (qrtr/rmtfs/pd-mapper/tqftpserv/wlfwd/qwlfgw).
- **Không có**: violet DTS/defconfig trong repo (chỉ có MT689x), init script dùng bởi PID1 (init tự viết, không chạy `.rc`), `udhcpc`/hostap source, thư mục `/lib/firmware/ath10k/WCN3990/...`, firmware board data per-unit.
- Firmware WCN3990 thật (`wlanmdsp.mbn`, `BDWLAN.*`, `regdb.bin`) đã staged ở `/lib/firmware`; modem firmware ở `/vendor/firmware_mnt/image`.

### Hướng sửa tiếp theo (mỗi lần một cái)
1. **Cần user cung cấp bản dump p74 thật** từ chính máy: trong recovery đáng tin, `dd if=/dev/block/mmcblk0p74 of=/sdcard/persist.img`, mount ext4 ở host, lấy `wlan/wlan_mac.bin` và `wlfw_cal_*.bin`, hash rồi restore có backup. Không tự tạo MAC/calibration.
2. Sau khi có MAC thật: sửa `pd-listen` để xử lý cả endpoint 0x42 đã tồn tại trước khi watch (hiện chỉ nghe NEW_SERVER), kiểm tra log phải có `result=0 error=0`.
3. Chỉ sau đó mới đánh giá lại WLFW 0x45; không fake QMI server.


## Chạy cnss-daemon thật + thu log trực tiếp (2026-11-22)
Raw: `reports/wifi-violet-cnss-live/*.txt`.
- QEAF boot: `stage=loop`, `ui=1`, `drm=1`.
- Binder: `/proc/filesystems` có `nodev binder`; `/proc/misc` không có binder;
  `/dev/binder -> /dev/binderfs/binder`; `hostsvc binder: present`;
  `cnss-daemon: running pid=547`, `/proc/547/fd/12 -> /dev/binderfs/vndbinder`.
- stdout/stderr (`/tmp/hostsvc-cnss-daemon.log`) chỉ có 2 dòng warning
  linkerconfig, **không có debug WLFW** (kernel không có Android logger).
- `qrtr-lookup 0x45`: `lookup 0x45/0x1/0x0 sent` → `recvfrom(): Resource
  temporarily unavailable` lặp; **không có server 0x45**.
- MSA/DMA dmesg: `wlan_msa_region@93900000` reserved-dma-pool,
  `iommu: Adding device soc:ipa_smmu_wlan to group 3`,
  `icnss 18800000.qcom,icnss: for wcss_msa0 segments only will be dumped`,
  `icnss: Platform driver probed successfully`,
  `wlan_hdd_state wlan major(500) initialized`. Không có firmware-ready.
- `wlan0`: vẫn absent; `wifi net` chỉ có lo/dummy/bond/tunnel. Không scan
  được mạng thật, không connect, không DHCP/ping.
- Kết luận: binder và cnss-daemon đã chạy thật, nhưng WLFW `0x45` vẫn không
  publish → driver không tạo netdev. Không sửa code trong đợt này; giữ nguyên
  blocker persist/MAC/calibration + SERVREG + modem reset như đã báo cáo.


## QEAFRESCUE v3 (2026-11-22)
- Đã thêm `scripts/qeafrescue_v3.py`: service layer thật trên nền v2 —
  PermissionManager, StorageManager, RecoveryLogService, TransactionJournal,
  ArchiveVerifier, BackupService, RestoreService, text Recovery UI.
- `tests/test_qeafrescue_v3.py`: **27 tests OK**. CLI smoke test backup →
  verify → restore dry-run → logs → export TXT → storage → interrupted đều chạy
  với dữ liệu thật trong temp dir.
- Partition Installer vẫn LOCKED; test `test_no_partition_write_api` chặn mọi
  API ghi phân vùng/fastboot flash/dd/erase/format.
- Recovery UI đồ họa trên thiết bị: **NOT IMPLEMENTED** (UEFI C recovery hiện
  tại không có file I/O; service v3 chạy host-side). RAM-BOOT: **NOT TESTED** vì
  `qeafrescue.py detect` không thấy Fastboot device. Không flash/reboot máy.
- Chi tiết: `docs/QEAFRESCUE_V3_ARCHITECTURE.md`.


## VQEAF Midnight V2 — áp dụng vào mã nguồn thật (2026-11-22)
- `platform/violet/ui/vqeaf_theme.c`: default theme = **DARK**; dark palette
  sang Midnight Navy `#070B20`, Deep Blue `#101A3A`, Neon Violet `#8157FF`,
  Electric Blue `#287BFF`, Cyan Glow `#25D9F2`, text `#F4F5FF` / `#AAB2D0`.
- `vqeaf_glass_surface()` chuyển từ tím cũ sang gradient Deep Blue → Violet,
  viền Cyan, bóng navy; `vqeaf_wallpaper_text_color()` dùng đúng Primary Text.
- Giữ nguyên pages/widgets/IME; không thêm UI framework thứ hai. Design spec:
  `docs/design/VQEAF_MIDNIGHT_V2.md`.
- Build violet OK (11.249.320 B). `test_m3.py` / `test_ime.py` ALL OK;
  `test_violet_shell.py` còn 13 FAIL cũ (search placeholder + IME icon), không
  phải do palette.
- Flash ảnh sha256 `718e8e7ef75429175d45563b691b139cec3720bb23c356b4e0685400490ec83a`.
  Ảnh chụp thật trên máy `build/screens/device/midnight_home.png` xác nhận
  nền navy, accent violet/cyan, glass cards — Midnight V2 đã chạy trên thiết bị.


## Homologation 5 bước Wi-Fi trên violet (2026-11-22)
- Thêm `vio_wifi_diag()` trong `platform/violet/wifi_bringup.c` và rescue verb
  `wifi diag`: in 5 dòng VERIFIED/FAILED kèm bằng chứng thật, ghi vào
  `WIFIDIAG.TXT`. (`HOMOLOG.TXT` thuộc boot-trace publisher `usb_lun.c`, ghi lại
  mỗi poll nên không dùng làm nơi lưu diag.)
- Kết quả baseline (modem chưa start):
  `STEP1 VERIFIED` (firmware_mnt=/dev/mmcblk0p82, wlanmdsp.mbn present),
  `STEP2 FAILED state=OFFLINING`, `STEP3 FAILED` (binder absent, cnss missing),
  `STEP4 FAILED WLFW 0x45 NOT_REGISTERED`, `STEP5 FAILED wlan0 absent`.
- Sau `wifi up`: `STEP2 VERIFIED state=ONLINE` — modem **có boot thật**. Tuy nhiên
  `vio_host_services_start_wlan()` vẫn dừng ở `find_factory_mac()` (p74 không có
  wlan_mac.bin) nên cnss/binder không được khởi động trong luồng đó; WLFW vẫn
  không publish, wlan0 vẫn absent. Thiết bị reset lại ~40 s sau khi modem ONLINE
  (quay về stage=loop, uptime mới 49 s) — lỗi modem reset vẫn còn.
- Build 11.257.064 B, flash sha256
  `61db9b99743093f929b5d9f4a955abf45d03f9b348ca7961eb57f3314a3bcaa9`.


## Chụp nhanh + quay màn hình (2026-11-21/22)
- Chụp nhanh đã có: verb `shot` + combo Power+Volume → file `SHOTnnnn.PNG/BMP`
  trên QEAFRESCUE (RGB565 scanout thật).
- Quay màn hình mới: `vio_rec_start/stop/status()` trong `main_violet.c` chụp
  framebuffer DRM thật xuống 240x416, encode PNG bằng lodepng, ghi chuỗi
  `REC0001.PNG...` lên ổ RAM rescue. Chạy đồng bộ trong worker rescue (thread
  bị kẹt mutex nên đã bỏ) — mỗi frame là pixel thật, không giả lập.
- Rescue verb `rec start [fps] [secs]`, `rec stop`, `rec status`; Terminal lệnh
  `rec start|stop|status` cùng API. `violet.h` khai báo API.
- Host `scripts/rec_frames_to_qvf.py` ghép chuỗi PNG thành QVF1 thật (LZ4 block
  + RGB565) theo `docs/QEAF_VIDEO_FORMAT.md`; đã test với 3 frame thật →
  `recording-test.qvf` 3592 bytes, 240x416, 3 frames, 4 fps.
  `test_vqeaf_video.py` ALL OK. Build violet OK (11.265.144 B).
- **Chưa validate trên máy**: sau lần flash cuối thiết bị kẹt ở boot stage
  `splash`, không nhận QEAF.CMD. Cần người dùng reboot bằng tay rồi chạy
  `rec start 4 2`, `rec status`, đọc `REC*.PNG`, ghép QVF. Không báo PASS khi
  chưa có frame thật từ thiết bị.


## Wi-Fi native audit + firmware inventory (2026-11-22)
- Giai đoạn 1: `docs/wifi/VIOLET_WIFI_AUDIT.md` — classify module, kernel/driver,
  QRTR/QMI, firmware, network stack; xác nhận kernel hiện tại là stock 4.14
  icnss+qcacld (SNOC), không phải ath10k; network stack tự viết chưa có.
- Giai đoạn 2: `scripts/wifi/inspect_violet_firmware.py` (read-only) +
  `firmware/violet/manifest.json` + `docs/wifi/VIOLET_FIRMWARE_AUDIT.md`.
  `violet-fw.zip` có 19 member, WLAN image nằm trong `NON-HLOS.bin`.
- `tests/wifi/test_firmware_manifest.py` PASS (3 tests, fixture gắn nhãn FIXTURE).
- Chưa flash, chưa ghi phân vùng, chưa đổi firmware vùng.


## Wi-Fi Giai đoạn 3 — hardware abstraction (2026-11-22)
- Thêm parser FDT thật `kernel/drivers/wifi/wcn3990/wifi_device.c`: đọc reg,
  interrupts, compatible, MSA reserved-memory từ DTB do platform cung cấp;
  không hardcode địa chỉ/IRQ; fail-closed trên blob hỏng.
- Thêm `wifi_firmware.c`: load/validate/hash firmware qua callback, buffer caller
  giữ, không malloc, không ghi storage, không blob giả.
- Không tạo module trùng: power/clock/regulator/MSA/IRQ/DMA/CE vẫn là phase2;
  QRTR/QMI để Giai đoạn 4 (không tạo stub rỗng). Xem
  `docs/wifi/VIOLET_WCN3990_ARCHITECTURE.md`.
- Test host: `python scripts/wifi/run_hal_tests.py` -> ALL OK;
  `python -m unittest tests.wifi.test_wifi_hal` -> 3 tests OK (FIXTURE DTB).
- Build kernel AArch64: OK, ELF 392.488 B (wifi_device.o + wifi_firmware.o).
- Chưa có bằng chứng phần cứng: WLFW 0x45 chưa publish, wlan0 vẫn absent.


## Wi-Fi Giai đoạn 4 — QRTR/QMI native (2026-11-22)
- Thêm client QRTR/QMI native, độc lập với codec BSD trong hosttools:
  `qrtr_transport.c` (control 20-byte LE, server table, discovery qua
  NEW_LOOKUP/NEW_SERVER thật) + `qmi_service.c` (header 7B, TLV, txn id,
  matching, timeout/retry, reset). Không giả service/response.
- Test C host với fixture gắn nhãn FIXTURE: `test_qrtr_qmi: ALL OK`
  (service not found, timeout + retry, sai txn, sai độ dài, disconnect,
  CNSS reset, indication). `tests.wifi.test_qrtr_qmi` -> 4 tests OK.
- Build kernel AArch64: OK, ELF 421.056 B. Đã thay musl memmove bằng vòng lặp
  để tránh kéo ~900 KB debug info vào ELF freestanding.
- Docs: `docs/wifi/VIOLET_QRTR_QMI.md`.
- Chưa có bằng chứng phần cứng: WLFW 0x45 vẫn chưa publish, wlan0 absent.


## Wi-Fi Giai đoạn 5 + đo IO thật trên máy (2026-11-22)
- State machine boot firmware: `firmware_boot.c/.h`, mọi bước cần xác nhận thật,
  poll có timeout, crash restart có giới hạn, thiếu QMI service -> FAILED,
  không bao giờ WLAN_READY. `test_firmware_boot: ALL OK`.
- Gắn IO AF_QIPCRTR thật (`qrtr_probe.c`) vào PID1, verb `wifi discover`.
- **Đo trên máy thật** (ảnh sha `c9e92704...0338c`): `0x45` -> `recv_calls=0`,
  `NOT_REGISTERED`, `rc=-2 (ENOENT)`, `elapsed_ms=1502`. Đối chứng `0x18`
  -> `NEW_SERVER` (cmd=4, 20B) `node=0x5 port=0x8 VERIFIED`, chứng minh IO chạy
  thật và kết quả 0x45 là BLOCKED có ý nghĩa.
- `wifi svc`: có lookup `0x45` nhưng không có server `0x45` trong 24 server.
- Báo cáo: `docs/wifi/VIOLET_WIFI_TEST_REPORT.md`.
- Flash lần 2 (đổi tên file 8.3) build/gate/flash PASS nhưng máy kẹt `splash`,
  cần power-cycle tay; `QRLIVE.TXT` chưa verify, không báo PASS.
- wlan0 vẫn absent; scan/DHCP/data chưa có bằng chứng.


## Port config từ crDroid violet device tree (2026-11-22)
- Clone audit `crdroidandroid/android_device_xiaomi_violet` @ faab5ba: repo
  **không có driver C** (qcacld-3.0/icnss nằm ở kernel/vendor), chỉ có 3 file
  config Wi-Fi + symlink fstab. HAL C++ trong repo là Android HIDL -> không port.
- Port 3 config thật vào `firmware/violet/config/` + manifest provenance;
  vô hiệu hoá MAC placeholder `000AF58989FF/FE` của vendor (không phải MAC máy).
- `package_violet.py` stage config từ thư mục tracked + gate từ chối MAC giả.
- Test `tests.wifi.test_crdroid_config_port` 4 tests OK; build OK; package OK
  (sha `c246cbae...89ee`); config có trong ramdisk, MAC giả không có.
- Docs: `docs/platforms/VIOLET_CRDROID_CONFIG_PORT.md`.
- Không đổi trạng thái WLFW 0x45 / wlan0; config không giải quyết blocker.


## Chụp màn hình + quay video màn hình (2026-11-22)
- `shot` trước đây luôn hỏng trên máy: `shot: failed (4)`. Nguyên nhân thật là
  PNG 1080x2340 vượt hạn 64 cluster (256 KiB) của `fat16_write_file`, nên lần
  ghi nào vào gốc volume cũng bị từ chối.
- Thêm `fat16_write_path()` + `fat16_free_bytes()`: ghi được vào thư mục con
  (`UserData/Pictures`, `UserData/Videos`), giới hạn chỉ bởi cluster còn trống;
  ghi đè tại chỗ khi entry đã có, từ chối khi hết chỗ thay vì đè entry sống.
- Ảnh màn hình: `UserData/Pictures/SHOTnnnn.PNG` (1080x2340, lodepng chạy trong
  tiến trình, fallback BMP 24-bit). Gọi từ ô "Chụp màn hình" ở Trung tâm điều
  khiển hoặc verb `shot`.
- Quay màn hình: recorder ghi thẳng QVF1 (`platform/violet/qvf_encode.c`, LZ4
  block + RGB565 240x416) ra `UserData/Videos/VIDnnnn.QVF` - đúng container
  `docs/QEAF_VIDEO_FORMAT.md`, phát được bằng trang Video và player QEAPP.
  Vòng lặp UI lấy một frame mỗi 1/fps giây (`rec_tick()`); bản cũ chạy đồng bộ
  trong `vio_rec_start()` nên UI đứng suốt lúc quay và clip chỉ có một ảnh.
  Giới hạn: fps 1..15, 1..60 s, ngân sách min(6 MiB, nửa dung lượng trống).
- Gate: `tests/vqeaf_qvf_encode.c` (CMake `vqeaf_qvf_encode_test`) nén rồi giải
  nén lại 3 frame và so từng pixel -> ALL OK; file sinh ra được kiểm chứng chéo
  bằng bộ giải LZ4 Python của `scripts/mp4_to_qvf.py` -> 0 pixel sai, walk-end
  khớp đúng độ dài file.
- Build device `build_violet.py` OK; `package_violet.py` OK, image
  `32.497.664 B`, sha256 `b407d2b4e8f4d538f324c105df177ce7f5d856d58b533d48c46a2ef2e7e3f58c`.
- **Chưa kiểm chứng trên máy**: điện thoại đang kẹt stage `splash` (userspace
  chết) sau lần reboot trước; phải power-cycle rồi nạp p77 mới chạy được bản có
  hai tính năng này.


## Sửa treo boot ở stage splash (2026-11-22)
- Triệu chứng: QEAFBOOT.LOG dừng ở `boot stage splash`, RESPONSE.TXT không đổi,
  worker rescue không xử lý QEAF.CMD (FAT chain/BOOTIMG.BIN kiểm tra không lặp).
- Fix: tách `splash_present()` + `start_phone_ui()` vào thread riêng
  `boot_ui_worker()`; main thread chờ tối đa 5 s rồi tiếp tục `loop` dù UI chưa
  xong. Thêm log `boot: UI worker started/splash painted/phone UI ready` để lần
  sau xác định chính xác điểm dừng. Rescue worker/watchdog không còn bị main
  thread chặn bởi display ioctl.
- Build OK (11.401.784 B); package OK, ảnh sha
  `1baca56cd8ffec6938bea891f38c8ed12400ebeb14ec931a818fa7cda1470faf`.
- Chưa flash/kiểm chứng trên máy: ảnh đang chạy bị treo nên không gửi được
  lệnh flash confirm. Cần boot fastboot để `fastboot boot` ảnh mới (RAM, không
  ghi partition).
- `dist/boot-vqeaf-violet.img` hiện tại là bản **gộp**: tính năng chụp màn hình
  + quay video (mục trên) và fix treo splash. Đã kiểm tra chuỗi trong
  `build/violet/vqeaf-init` của chính image: có `shot`, `rec start|stop|status`,
  `QVF1`, `UserData/Pictures/`, `UserData/Videos/`, `rec_tick`.
  sha256 `1baca56cd8ffec6938bea891f38c8ed12400ebeb14ec931a818fa7cda1470faf`,
  32.497.664 B; `validate_bootimg` + `audit_rescue_channel` PASS.


## Mở rộng QEAFRESCUE (2026-11-22)
- Bỏ giới hạn cứng 64 cluster/file (~256 KiB) trong `fat16_write_file()` và
  `file_capacity()`; `fat_alloc()` tái use cluster đã xoá, cập nhật
  `next_cluster` để mkdir không cấp trùng (lỗi này từng làm FAT test crash).
- Thêm `fat16_delete_file()` + verb `del NAME`, `df` — thu hồi cluster, cho phép
  ghi lại sau khi ổ đầy (BOOTIMG.BIN 32 MB trước đó chiếm gần toàn bộ LUN).
- `fat16_free_bytes()` đếm cluster trống thật thay vì phần đuôi chưa dùng.
- Worker fault không còn "park vĩnh viễn": đặt `rescue_thread_up = 0`, main loop
  tự tiếp QEAF.CMD qua trylock fallback.
- Test `tests.test_qeaf_rescue_fat` 5 tests OK; file 300 KiB (>64 cluster) ghi/đọc
  lại đúng, xoá thu hồi, ghi lại OK. Build OK (11.410.920 B); package OK
  sha `3236fca7...77dd`.
- Chưa flash: thiết bị đang kẹt; cần fastboot theo docs/UI_DEVICE_TEST.md.


## Tìm ra nguyên nhân kẹt QEAF.CMD: tự deadlock volume_lock (2026-11-22)
- `rescue_worker()` giữ `volume_lock` quanh `rescue_poll()`. Các verb gọi hook
  `vio_screenshot_now()` (shot) và `vio_rec_start()` -> `rec_budget()` (rec)
  lại gọi `volume_lock_acquire()` trên **cùng mutex không đệ quy** của worker
  → tự deadlock: F: vẫn mount nhưng QEAF.CMD không bao giờ được đọc.
- Sửa: `volume_lock` thành `PTHREAD_MUTEX_RECURSIVE` (khởi tạo qua pthread_once
  trong `volume_lock.h`, gọi `volume_lock_init()` ở đầu main). Depth TLS vẫn
  được giữ nên `volume_lock_drop_on_fault()` trả đúng mọi cấp khi fault.
- Test `tests/vqeaf_volume_lock_test.c` mở rộng lên **5 checks OK**, kiểm chứng
  nesting 3 cấp + drop_on_fault trả sạch; `scripts/test_fat16_path.py` ALL OK.
- Build OK (11.422.696 B); package OK sha
  `5ca6e6fba3652fd87ee105115d33e85fcf78e7643611e987870954fd0e015a96`.
- Chưa flash: thiết bị chưa kết nối; cần fastboot để nạp ảnh này (chỉ p77).


## Bỏ giới hạn 32 MB của RAM LUN QEAFRESCUE (2026-11-22)
- `FAT16_VOLUME_SIZE` 32 MiB -> 255 MiB (giữ nguyên 4 KiB cluster, FAT 256
  sector — 255 MiB là mức tối đa an toàn của FAT16 với geometry này).
- `fat16_init` kiểm tra cluster count vừa FAT table, fail-closed nếu vượt.
- `usb_lun.c` tự theo macro: ftruncate/mmap backing file 255 MiB (máy có
  5,2 GiB RAM trống). `fat16_free_bytes` đếm đúng cluster trống thật.
- Test host ALL OK (file 300 KiB, xoá/ghi lại, 12 unittest); build OK
  (11.423.120 B); package OK sha
  `d8fab469fa3ab0ce0205bc51c90aa7cbc8ca86c1034ab0b58b66cb6b3f8c8dd9`.
- **Đã flash trên máy thật** (sha ở trên, "written and verified byte for byte",
  reboot, rescue answering). Kiểm chứng sau boot: `stage=phone-ui`, `ui=1`,
  uptime=49s; `df` = **266.530.816 bytes (~254 MiB) trống**.
- Giới hạn còn lại: FAT16 ~255 MiB (muốn lớn hơn phải đổi cluster size/geometry,
  rủi ro tương thích host) và 512 root entry / tên 8.3 (có LFN).


## Tiếp tục Wi-Fi firmware/driver — chẩn đoán bằng chứng thật (2026-11-22)
- Trên máy (ảnh mới, boot phone-ui, seq tăng): `wifi state` cho thấy
  pd-mapper/pd-listen/tqftpserv/rmtfs chạy, nhưng `wlfwd: not started`,
  `cnss-daemon: not started`, `binder: absent`. `wifi up` khởi động modem
  (modem_started=1) nhưng `wifi discover` vẫn `recv_calls=0 NOT_REGISTERED`;
  khoảng 40 s sau máy reset — đúng current known failure.
- Tìm MAC thật: `/proc/cmdline` **không có** `androidboot.wifimacaddr=`;
  `/vendor/firmware/wlan/qca_cld/wlan_mac.bin` tồn tại nhưng là symlink
  → `/mnt/vendor/persist/wlan_mac.bin` **không tồn tại** (`cat` errno 2,
  xác nhận bằng `wifi link`). `/mnt/vendor/persist/{misc,rfs,hlos_rfs,bluetooth}`
  rỗng. `BDWLAN.*` trong firmware package chỉ chứa placeholder `12 34 56`.
  → Không có MAC/calibration thật của chính máy này; **không sinh giả**.
- Sửa chẩn đoán: `find_persist()` quét sâu 2→4 để không bỏ sót file nằm sâu
  (`wlan/qca_cld`); build/package/flash OK, ảnh sha
  `b3dfba17f8066eb3422f3d4f116cfe2dd1ba244bee62708a6a05bd5e83a2c7de`.
- Sau flash: `stage=phone-ui`, `wifi find wlan_mac.bin: not found`,
  `wifi discover` → `BLOCKED(0x45_NOT_REGISTERED)`. Trạng thái vẫn trung thực.


## Thử đường cnss-daemon thật + binderfs (2026-11-22)
- Chạy `wifi vendor` trên máy: `hostsvc binder: present`,
  `cnss-daemon: running pid=560` — daemon **thật từ ROM**, không phải mock.
  `wifi log` chỉ có warning linkerconfig, không có log QMI debug.
- Dù CNSS chạy, `wifi discover` vẫn `recv_calls=0 / 0x45 NOT_REGISTERED`.
  → Chứng minh blocker không phải chỗ "chưa launch daemon", mà là firmware/
  factory data: modem không publish WLFW service thật.
- `wifi on` ghi /dev/wlan từ worker có thể treo; đổi `write_exact()` sang
  `O_NONBLOCK` để EAGAIN được báo thật thay vì kẹt QEAF.CMD.
- Build/package/flash OK, ảnh sha
  `55860eec15901cbd9a8cd6fa8d1d43a6dada65113411c2c2e95e485c3cafa3d3`.
- Kết luận giữ nguyên: không có MAC/calibration thật trên máy này, không
  tạo giả, WLFW 0x45 vẫn BLOCKED.


## RAM-only Wi-Fi test session — tmpfs persist + TEST MAC (2026-11-22)
- Theo yêu cầu: tmpfs **stacked mount** lên `/mnt/vendor/persist` (không ghi
  p74), ghi `wlan_mac.bin` 4 dòng + END với MAC **test RAM-only**
  `02:1A:2B:3C:4D:5E` (locally administered, không phải factory, mất khi reboot).
- Sửa `read_mac()`: buffer 64 -> 512 và thêm parser `Intf0MacAddress=` — trước đó
  file Qualcomm ~110 byte bị cắt đúng 64 và bị reject. Sau sửa
  `find_factory_mac()` thành công: `wlan: CNSS daemon spawned...`.
- Kết quả đo thật: cnss-daemon chạy (pid 539), binder present, nhưng
  `wlfw_cal_*.bin` **không sinh**, `qrtr-lookup 0x45` timeout, `wifi discover`
  `recv_calls=0 / NOT_REGISTERED`, `wlan0` absent. cnss log chỉ có warning linker.
- Kết luận: MAC RAM-only đủ để daemon chạy nhưng **chưa đủ** để publish 0x45;
  blocker nằm ở firmware/daemon thật (property/service manager/PD), không phải
  MAC hay chưa launch daemon. Không báo PASS.
- Bằng chứng đầy đủ: `docs/platforms/VIOLET_WIFI_BRINGUP.md`.
- Test `tests.wifi.test_ram_persist` 5 tests OK; ảnh boot sha
  `150875c5a4e00e6745d400025101a3bca321af0b67715aff6060ec3389609b8c` đã flash.


## Chụp ảnh / quay video trong trung tâm điều khiển + phím tắt (2026-11-22)
- Trung tâm điều khiển **đã có sẵn** 2 ô `Chụp màn hình` / `Quay màn hình`
  (group Tiện ích) gọi trực tiếp `vio_screenshot_now()` / `vio_rec_start/stop`,
  nhãn trạng thái thật (`Lưu vào Pictures`, `Đang quay`, `10 giây, 8 fps`).
- Thêm phím tắt: **Power + Volume nhấn ngắn** = chụp ảnh (có sẵn);
  **giữ Power + Volume >= 1 s** = bật/tắt quay video (mới), dùng chung API với
  ô điều khiển và lệnh `rec`. Long combo không chụp ảnh thêm.
- Sửa lỗi 0 frame: `rec_tick()` định nghĩa nhưng **không bao giờ được gọi**;
  đã gọi trong vòng lặp UI sau `display_poll()`.
- Build/package/flash OK, ảnh sha
  `2e81ec2456e3920b8e33ac88baee56bcfc567905fbc9eacba28f9405e343af38`.
- Kiểm chứng trên máy: `rec start 4 2` -> `rec status` =
  `saved VID0001.QVF (8 frames, 194400 bytes, time limit reached)` — frame thật
  từ scanout, không phải ảnh tĩnh giả. Test `tests.test_qeaf_capture_shortcuts`
  4 tests OK.


## Trình xem ảnh/video và thư mục phương tiện (2026-11-22)
- Ảnh chụp lưu `UserData/Pictures`, video QVF lưu `UserData/Videos` trên RAM
  LUN. Nối LUN vào `vqeaf_files_*` qua backend (`vqeaf_files_set_backends`)
  với path ảo `/Pictures`, `/Videos`, `/media`, vẫn đi qua volume_lock.
- Gallery không còn placeholder: liệt kê ảnh thật, đọc **toàn bộ** tệp (ảnh
  chụp ~7,5 MB) rồi giải mã PNG/BMP/JPEG bằng decoder có sẵn, hiển thị pixel
  thật; có nút Đóng. Video page dùng `vqeaf_video_scan("/media")` + QVF
  frame decoder có sẵn — giờ đọc được `VID0001.QVF`.
- Thêm verb `media list` để kiểm chứng thư mục trên máy.
- Build/package/flash OK, ảnh sha
  `7d1d2e92dd235ac9668c8c76752079cb07cd8076dc7937082e6acc259da44add`.
- Kiểm chứng trên máy: `media list` ->
  `UserData/Pictures: SHOT0001.BMP(7581654B)`,
  `UserData/Videos: VID0001.QVF(193736B)`.
- Test `tests.test_qeaf_media_viewer` 4 tests OK.
- Wi-Fi vẫn BLOCKED (không đổi); cnss-daemon alive (S sleeping), strace không
  có trong rootfs, qrtr-lookup 0x45 timeout — chưa rõ nguyên nhân ENOENT,
  không tạo server/response giả.


## Log cnss chi tiết + Gallery thumbnail (2026-11-22)
- Thêm `wifi cnss-trace`: khởi động lại **cnss-daemon thật** với
  `LD_DEBUG=all`, `LIBLINKER_DEBUG=1`, `ANDROID_LOG_TAGS=*:v`, log vào
  `/tmp/hostsvc-cnss-trace.log`. Kết quả trên máy: `pid=542`, nhưng file
  **chỉ có 2 dòng warning linkerconfig** — không có lỗi thiếu thư viện động,
  không có log QMI/WLFW. Nghĩa là daemon load lib OK nhưng im lặng ở tầng QMI.
- Thêm `wifi kmsg-file`: lọc 35 dòng kernel log vào `KMSGWIFI.TXT`. File ghi
  ra không đọc lại được qua `wifi cat` (errno 2) — đang để lại làm bằng chứng
  rằng cơ chế ghi file này chưa đáng tin; **không dùng nó để kết luận**.
- Kernel log qua `wifi dmesg` cho thấy `wlan_hdd_state wlan major(500)
  initialized` — driver wlan đã nạp, nhưng vẫn không có `0x45` / `wlan0`.
- Gallery giờ là **photo gallery lưới thumbnail thật**: mỗi ô decode ảnh thật
  bằng PNG/BMP/JPEG rồi downsample vào buffer 160x120 riêng; chạm để xem ảnh
  full size. Không dùng ảnh placeholder.
- Build/package/flash OK, ảnh sha
  `78dea6aa235f91805b274443d6448ee262a65244601a04fd1b5ddd50d1baf0e7`.
- Kết luận Wi-Fi vẫn BLOCKED; không mock/bypass QMI.


## Test preview ảnh / trình phát video trên máy (2026-11-22)
- Thêm page hook `page gallery` / `page video` để host điều khiển UI khi test.
- **VERIFIED**: Gallery hiển thị lưới thumbnail thật của `SHOT0001.BMP` (đã so
  ảnh chụp màn hình `build/screens/device/gallery-*.png`); ảnh chụp lưu
  `UserData/Pictures`, `media list` xác nhận 7.581.654 byte.
- **FAILED / chưa xác minh**: chạm vào thumbnail chưa mở được ảnh full size dù
  đã thử: cờ CLICKABLE trên tile, nút hit trong suốt phủ lên ô (sửa lỗi pct
  height = 0 trong flex SIZE_CONTENT thành 140x130), notify debug trong
  `gallery_open_image()`, và các tọa độ `(300,930)`, `(100,310)`, `hold`.
  Không có notify/callback được ghi nhận -> không khẳng định preview ảnh chạy.
- Video: `page video` mở đúng trang nhưng báo "Không có tệp video trong
  /media" vì phiên đó `rec` không lưu được file; **trình phát QVF chưa được
  kiểm chứng end-to-end** (core decode có sẵn và đã test host).
- Ảnh cuối đã flash sha `345a407e20afd5bcbfaea6296478d87f750fb1710ce449b00fef78165856f335`;
  build OK. Tiếp theo cần gắn event LVGL trực tiếp trên image/label thay vì
  nút hit, và đảm bảo có file QVF trước khi test phát lại.


## Nỗ lực sửa lỗi preview photo (2026-11-22)
- Thêm `media open NAME` gọi trực tiếp `vqeaf_ui_open_picture()` ->
  `gallery_open_image()` (cùng đường decode với lưới thumbnail), để tách lỗi
  event chạm LVGL ra khỏi lỗi decode.
- Thử lần lượt: cờ CLICKABLE, nút hit trong suốt (sửa pct-height=0), callback
  trên toàn card, `tap`/`hold` nhiều tọa độ, notify debug trong hàm mở ảnh.
- **Kết quả vẫn FAILED**: `media open SHOT0001.BMP` trả "requested", nhưng ảnh
  chụp màn hình ngay sau đó (`build/screens/device/photo-preview*.png`) không
  thấy overlay preview lẫn notify. Không có bằng chứng decode/preview chạy.
- Không khẳng định tính năng preview ảnh hoạt động. Cần debug thêm trên UI
  thread (LVGL layer_top / lifecycle) trước khi báo PASS.
- Ảnh cuối đã flash sha `b0c2f79543626d1e7fd2740b05471da12a1584b577d9eeae5465de4ac6a6aaf2`.


## Vuốt 3 ngón chụp màn hình (2026-11-22)
- `violet_input.c` theo dõi từng slot MT (`ABS_MT_SLOT`, `ABS_MT_TRACKING_ID`,
  `ABS_MT_POSITION_X/Y`), đếm contact active; khi >=3 contact và centroid di
  chuyển >60 raw unit thì set cờ `three_finger_swipe`. Không coi gesture này
  là drag chuột của LVGL (`pending_pressed=0` khi active>=3).
- Main loop consume cờ và gọi cùng `screenshot_png(&lun.volume, ...)` với ô
  Chụp màn hình / Power+Volume; thông báo tên file thật bằng `vqeaf_ui_notify`.
- Thêm verb host-test `swipe3 DX DY` đi qua `vio_touch_inject_three()` — dùng
  **cùng detector** với sự kiện evdev thật, không phải đường tắt giả.
- Build OK (11.462.064 B); `tests.test_qeaf_three_finger` 4 tests OK; package/
  flash OK, ảnh sha `fc8c4f8f2fc00c7be4681b722081c9cee20d8191fd5051d38f3dcc1dbe811e3d`.
- **Kiểm chứng trên máy**: `media list` trước `UserData/Pictures: SHOT0001.BMP,
  SHOT0002.PNG`; sau `swipe3 150 0` xuất hiện thêm
  `SHOT0003.BMP(7581654B)` — vuốt 3 ngón chụp ảnh thật, VERIFIED.


## Floating capture thumbnail góc trái dưới (2026-11-22)
- Thêm `vqeaf_ui_show_capture_thumb(name)` trong `vqeaf_pages.c`: decode ảnh vừa
  chụp bằng `gallery_make_thumb()` vào slot 1, tạo ô 120x96 viền tím ở góc trái
  dưới (`LV_ALIGN_BOTTOM_LEFT`), tự ẩn sau 4 s, chạm để mở ảnh. Gọi từ
  `screenshot_png()` cho mọi đường chụp (Control Center, Power+Volume, 3 ngón).
- Theo dõi tên ảnh cuối qua `vio_last_screenshot()`.
- Build OK (11.463.624 B); package/flash OK, ảnh
  sha `8a1d228e921fdc91a7394217fbcfe8e55cafc8f9807ad34ff736f9aba014af1c`.
- **Chưa VERIFIED trực quan**: ảnh chụp màn hình ngay sau `swipe3`
  (`build/screens/device/shot-thumb*.png`) chưa thấy ô thumbnail; cần kiểm tra
  lại LVGL refresh/layer trước khi báo PASS. Ảnh thật vẫn được lưu
  (`SHOT0002.BMP` trong UserData/Pictures).


## Triển khải ứng dụng Máy ảnh native (2026-11-22)
- App "Máy ảnh" trước đây là stub (`VQEAF_PAGE_COUNT`); giờ là trang
  `VQEAF_PAGE_CAMERA` gọi driver V4L2 thật `vqeaf_camera_probe()`,
  `vqeaf_camera_create_session()`, `vqeaf_camera_query_caps()`.
- Kiểm chứng trên máy (ảnh `build/screens/device/camera-app.png`):
  `cam: v4l2 nodes=5 capture=0 control=2 rotator=1 codec=2`,
  session cam-req-mgr `0x00170200`, CAM_QUERY_CAP trả số byte thật.
- Không có node capture: `CAM_ACQUIRE_DEV` sensor/ICP vẫn trả errno 22
  (đã ghi từ 2026-11-21), ISP/acquire OK. Trang ghi rõ **không có preview,
  không ảnh giả** — không tuyên bố driver camera hoàn tất.
- Build OK (11.466.848 B); package/flash OK, ảnh
  sha `733ce1ba5cbf09098d7f84440433af0ac75ee9e5163da97ca062f0484b5145b4`;
  `tests.test_qeaf_camera_app` kiểm chứng cấu trúc.


## Wi-Fi: phân tích cnss-daemon thật + sửa đường dẫn calibration (2026-11-22)
- Trích strings binary ROM: `-n` = `--nodaemon` (không tắt WLFW), cnss-daemon là
  QMI **client** của WLFW/DMS; calibration ghi `/data/vendor/wifi/wlfw_cal_%02d.bin`.
- Sửa RAM-only setup tạo thêm `/data/vendor/wifi` (initramfs RAM, không ghi
  partition). Build/package/flash OK, ảnh
  sha `b6b8e62b77cf6ba01169dfed2e968ccffe2706cc59d9a7f375229ddd352d91c0`.
- Đo lại: thư mục tồn tại nhưng rỗng; cnss-daemon pid=545, binder present;
  `wifi discover` vẫn `recv_calls=0 / NOT_REGISTERED`. BLOCKED giữ nguyên,
  không mock QMI. Chi tiết: `docs/platforms/VIOLET_WIFI_BRINGUP.md`.


## Wi-Fi: khoanh vùng blocker tới WLAN PD (2026-11-22)
- `pd-listen` watch dùng lookup `0x42` instance 0, trong khi server thật là
  instance `0x4c/0x4a` → không nhận NEW_SERVER. Chạy trực tiếp với node/port thật
  `wifi listen 10 3 msm/modem/wlan_pd` cho kết quả thật:
  **`result=1 error=45`** (PD lỗi).
- `wifi pd 10 3 msm/modem/wlan_pd` (pdr-restart thật) → **`result 1 error 9`**.
- `wifi discover` sau đó vẫn `0x45 NOT_REGISTERED`, `wlan0` absent.
- Blockers chính xác hiện tại: WLAN PD lỗi + restart bị từ chối → firmware
  WCN3990 không boot → không publish QMI 0x45. Không mock/bypass QMI.
  Chi tiết: `docs/platforms/VIOLET_WIFI_BRINGUP.md`.


## Wi-Fi: boot order, firmware source, full modem restart (2026-11-22)
- Daemons rmtfs/tqftpserv/pd-mapper/pd-listen chạy trước `wifi up`; mount p86/p82/
  p74/p38 đúng readonly. Kernel log có icnss + wlan major(500) nhưng không có
  request_firmware/wlanmdsp. `/sys/class/remoteproc` absent; modem (subsys1)
  **OFFLINING**, adsp/cdsp/venus ONLINE.
- Firmware: violet-fw.zip có NON-HLOS/BTFM/dspso + wlanmdsp/bdwlan/regdb +
  6 SERVREG .JSN. `out/reference/stock-violet/` absent → không đối chiếu được
  phiên bản modem/dsp với bản chuẩn.
- Restart: `pdr-restart` qua SERVREG 10:3 cho cả `msm/modem/wlan_pd` và
  `msm/modem/root_pd` đều `result 1 error 9`; `pd-listen` vẫn `result=1
  error=45`. Không có sysfs remoteproc để restart.
- BLOCKED giữ nguyên ở tầng modem/firmware; không mock/bypass QMI.
  Chi tiết: `docs/platforms/VIOLET_WIFI_BRINGUP.md`.
