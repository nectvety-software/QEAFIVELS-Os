# VQEAF OS — thiết kế giao diện LVGL

## Mục tiêu

Giao diện smartphone native cho cổng Violet, dùng LVGL 9.4.0 và lấy bảng thiết kế
`uiux/546546.png` làm tham chiếu trực quan. QeafdropOs là nguồn tham khảo về
cách tổ chức status bar, launcher, app grid, control center và các trang ứng dụng;
không liên kết runtime với QeafdropOs.

## Quy chuẩn giao diện

- **Liquid Glass** là bề mặt mặc định của shell stock trong phiên: Home, màn hình
  khóa và Control Center dùng alpha, gradient, viền sáng và bóng đổ; Home giữ
  khung icon bo 10 px và dock có đĩa kính bo tròn. Fluent thay thế bằng acrylic.
  Renderer chưa có backdrop blur/refraction, nên kính được mô phỏng bằng alpha
  và gradient, không phải làm mờ nền thật. Tùy chọn mất khi khởi động lại.
- Wallpaper stock là gradient cobalt xây bằng hình LVGL, từ `#0A3EB4` đến
  `#4F9BFF` cùng các mảng màu procedural; không còn chọn ảnh Trái Đất RGB565.
- Biểu tượng ứng dụng hệ thống dùng bộ pixel art gốc 32×32 RGB565 được tạo bằng
  VPEPixel và nhúng trực tiếp vào LVGL. Nguồn sửa là 29 tệp `.vpe` trong
  `assets/icons/vqeaf_os_pixel/`; ID không hợp lệ vẫn quay về glyph dự phòng.
  Icon QEAPP do gói cung cấp giữ nguyên ARGB8888 và khử răng cưa. Trình xem
  ảnh/tệp vẫn giữ khả năng hiển thị nội dung do người dùng mở.
- Canvas dọc 720×1560, tương ứng panel 1080×2340; canvas ngang 1560×720.
- Mặc định Dark với nền `#071D4A`, surface `#123C7A`, accent `#5EA0FF`, chữ
  trắng; Light vẫn chọn được với nền `#EAF3FF`, chữ `#0B2447` và accent
  `#1663D6`. Bảng màu động vẫn lấy seed từ wallpaper cobalt thay vì ảnh cũ.
- Font Ubuntu có glyph tiếng Việt; Material Icons cho biểu tượng.
- Status bar và navigation bar cố định; vùng nội dung cuộn độc lập.
- Mọi số liệu phần cứng chỉ hiển thị khi provider có dữ liệu. Wi-Fi, pin, thời tiết
  không có backend sẽ hiển thị trạng thái chưa biết, không giả lập số liệu.

Quy trình kiểm thử trên máy thật (build → render → đóng gói → gate → checklist
đối chiếu SCREEN.RAW) nằm trong `docs/UI_DEVICE_TEST.md`; công cụ so sánh là
`scripts/ui_snapshot_compare.py`.

## Kiến trúc

- `platform/violet/main_violet.c`: khởi tạo LVGL sau splash, gắn display buffer,
  flush callback DRM/KMS, evdev pointer input và gọi `lv_timer_handler()`.
- `platform/violet/lv_conf.h`: cấu hình LVGL 32-bit XRGB8888 để khớp buffer panel;
  LVGL dùng heap riêng 8 MiB.
- `platform/violet/ui/vqeaf_ui.c`: status bar, navigation bar, page container,
  định tuyến điều hướng và cập nhật trạng thái định kỳ.
- `platform/violet/ui/vqeaf_pages.c`: thiết lập lần đầu, Lock, Home, Apps,
  Control Center và Settings.
- `platform/violet/ui/vqeaf_theme.c`: palette (kể cả vai trò Material 3), card,
  tile, typography, wallpaper, và design system M3 (shape/elevation/state
  layer/ripple + button/FAB/chip/switch/card).
- `platform/violet/ui/vqeaf_components.c/.h`: bộ 14 thành phần Liquid Glass
  tương tác dùng chung, state riêng theo owner và overlay có quản lý vòng đời.
- `platform/violet/ui/vqeaf_state.c`: clock, battery, Wi-Fi, storage, uptime và
  backlight provider.
- `platform/violet/ui/fonts/`: Ubuntu và Material Icons được biên dịch tĩnh.

## Thiết lập lần đầu (2026-11-26)

Video tham chiếu dài 61,5 giây chỉ lặp một khung 1080×1920: lời chào sáng,
biểu tượng mặt cười, nút Bắt đầu và liên kết Gọi khẩn cấp. Setup giữ đúng tinh
thần màn hình vào đó nhưng dùng nhận diện QEAFIVELS; không sao chép logo, asset
hay các bước không xuất hiện trong video.

- Năm bước nằm trong `VQEAF_PAGE_SETUP`: Xin chào, Bảo mật/PIN tùy chọn, Mạng,
  Giao diện và Hoàn tất. ID trang được nối thêm để không làm đổi ID ứng dụng và
  recents cũ.
- Khóa `setup.complete` do Settings Service sở hữu. Khi chưa hoàn tất, router
  trung tâm chuyển mọi trang khác về Setup; chỉ PIN và Wi-Fi đang mở như subpage
  tạm thời được phép. Setup/subpage không vào recents.
- Setup dùng toàn canvas: ẩn status bar, taskbar, dock, gesture pill và điều
  hướng ảo; chặn gesture, status tap, Home bar, Back/Home/Recents, sleep/wake và
  thao tác Lock trong power menu. Sau khi hoàn tất, chrome thường được khôi phục.
- PIN dùng keypad và khóa `lock.pin` thật. Settings từ chối ghi thì PIN không
  được coi là đã đặt; backend ghi-through lỗi nhưng RAM đã nhận thì UI cảnh báo
  rõ thay đổi chỉ tồn tại trong phiên.
- Mạng gọi `vqeaf_wifi_scan()` và hiển thị interface, driver, firmware, link và
  lỗi quét thật. Nút trạng thái mở đúng trang Wi-Fi hiện có rồi quay lại bước
  Mạng. Không có mạng/provider thì hiển thị thiếu, không tạo kết nối giả.
- Gọi khẩn cấp báo chưa có dịch vụ thoại/modem; không giả lập cuộc gọi. Giao
  diện Sáng/Tối gọi `vqeaf_ui_set_theme()` và áp dụng ngay.
- `vqeaf_setup_complete()` trả `-1` nếu Settings từ chối marker, `0` nếu marker
  được chấp nhận, `1` nếu RAM đã nhận nhưng ghi bền vững thất bại. Trường hợp
  `-1` giữ nguyên Setup; `1` cho hoàn tất phiên kèm cảnh báo.
- Violet hiện chưa gắn backend Settings bền vững: Setup, PIN và theme mất sau
  reboot. Không ghi marker vào p77 ad-hoc vì extent appstore 32–64 MiB còn chồng
  boot journal ở 64 MiB−16 KiB; xung đột này vẫn phải xử lý trước khi hứa lưu
  bền vững.

Kiểm chứng host ngày 2026-11-26: `python scripts/test_violet_shell.py` PASS
(`ALL OK`, 83 snapshot), gồm boot đầu, chặn bypass, PIN đúng/sai/từ chối/hủy,
Wi-Fi thật và quay lại, đổi theme, từ chối marker, lỗi ghi-through và mô phỏng
reboot. `python scripts/render_phone_ui.py` PASS 236 PNG ở 720×1560/1560×720 với
profile `unknown`/`providers`; các khung setup đã được xem trực tiếp. Build
Violet PASS 292 object, `/init` 12.639.296 byte, SHA-256
`20ba713550b0cc8f1a60fc0f44fec9d93a005627156271bbcf01120952d325e1`; MT689x
PASS 285 object, `/init` 12.430.592 byte, SHA-256
`6cfc4611571c0686ff646d9087b6eaee879f440f5dc70c9b744722c4d0a34db9`. Package
51.679.232 byte, SHA-256
`3b5678ca3ed7fb0ce816e8a5d6da0d18cf985d903dcc30ef237ce633efce229a`, PASS
`validate_bootimg`/`audit_rescue_channel` và verify-only; manifest vẫn ghi
`physical_boot_tested=false`. Chưa nạp image hay kiểm thử setup trên thiết bị.

## Launcher Cobalt Glass theo ảnh tham chiếu (2026-11-26)

Home dùng bố cục launcher toàn màn hình lấy cảm hứng từ ảnh tham chiếu cobalt,
giữ danh tính VQEAF và tên ứng dụng tiếng Việt; không thêm nhãn hiệu, icon,
thông báo hay dữ liệu thời tiết của ứng dụng nền tảng khác. Phần này thay thế
mô tả Home/dock Midnight ảnh Trái Đất bên dưới. Home Fluent vẫn là bố cục
Start/taskbar riêng.

- Wallpaper gradient cobalt procedural phủ toàn màn hình, kể cả dưới status bar.
  Home có tiện ích hệ thống thật gồm uptime, RAM đã dùng/tổng và phần trăm lưu
  trữ; không có thời gian sử dụng ứng dụng hay dữ liệu phần cứng giả.
- Lưới ứng dụng thật có 4 cột × 4 hàng khi dọc và 8 cột × 2 hàng khi ngang.
  Registry có 30 ứng dụng: Home chia 16 mục/trang (16 + 14), còn Apps drawer
  chia 8 mục/trang (bốn trang). Không nhân bản icon hay thêm app giả.
- Chấm trang nằm phía trên ô tìm kiếm. Dock cố định bốn vị trí, mặc định là
  Trình duyệt, Máy ảnh, Thư viện và Nhạc; dock stock có đĩa kính bo tròn, còn
  Fluent dùng taskbar acrylic. Giữ rồi kéo giữa bốn slot để đổi chỗ; giữ tại chỗ
  vẫn mở thông tin, và thao tác “Di chuyển” trong hộp thông tin vẫn dùng chế độ
  chạm-chọn slot. Trạng thái trang/dock chỉ nằm trong RAM initramfs nên reboot về
  mặc định.
- Nhấn giữ vùng trống của wallpaper mở chế độ Sửa. Overlay có wallpaper toàn
  màn hình, khung xem trước bo tròn chứa đúng pager Home thật, cùng ba nút Hình
  nền, Tiện ích và Xong. Vuốt ngang trong xem trước dùng chung trạng thái trang.
  Hình nền mở bộ chọn hiện có; Tiện ích chưa có picker/backend nên giải thích
  thay vì tạo widget giả.
- Chạm ngắn icon có trang tương ứng mở ứng dụng. Ứng dụng chưa có trang nội dung
  vẫn hiển thị nhưng mờ đi và có badge `!`; chạm đưa ra snackbar giải thích thay
  vì điều hướng giả. Nhấn giữ icon mở thông tin ứng dụng và chặn lan truyền để
  không đồng thời mở chế độ Sửa. Lưu trữ và Thông tin nối đúng trang thật.
- Biểu tượng hệ thống dùng bộ 29 pixel art VPEPixel 32×32 RGB565 mô tả ở mục
  Quy chuẩn; Home, Apps, dock, thông tin ứng dụng và recents dùng chung asset này.

Kiểm chứng host ngày 2026-11-26: `python scripts/test_violet_shell.py` PASS
(`ALL OK`, 77 snapshot), `python scripts/render_phone_ui.py` PASS với 216 PNG
trong `build/screens/phone-ui/`; `python scripts/build_violet.py` PASS, `/init`
12.625.040 byte, 292 object, SHA-256
`d2e0ec3fbc3bf041dbfb669a7c31df6654805bd5235b4bd06f3c4258c42b6a35`.
Bốn khung kéo-thả mới đã được xem trực tiếp. Đây là bằng chứng host/build, tách
biệt với bằng chứng thiết bị vật lý bên dưới.

Ngày 2026-11-26, ảnh `dist/boot-vqeaf-violet.img` 51.445.760 byte, SHA-256
`1b72829fd7f0725cd1b0d9d647fc27fade604a7f5508e7307dd65b94e24f5b55`, đã qua
`validate_bootimg` và `audit_rescue_channel` rồi được nạp **chỉ vào p77/recovery**
trên Violet `ca1af25d`. Rescue worker trả `flash: written and verified byte for
byte`; `QEAFBOOT.LOG` trước reboot xác nhận `lun: verified - recovery and
BOOTIMG.BIN agree byte for byte over 51445760 bytes`. Không ghi `p76/boot`,
`userdata`, `misc`, firmware hoặc bootloader.

Sau reboot, `HOMOLOG.TXT` xác nhận PID 1, USB mass-storage `a600000.dwc3`, DRM
1080×2340, splash/LVGL 720×1560; gate vật lý PASS 11/11. Ảnh chụp trực tiếp nằm
trong `build/screens/device/cobalt-glass-20261126-075520/`: `lock-device.png`
(SHA-256 `bbd0fbb7f425b6dc0eaf1dbc174d9445507fd16f196f42bd89bf91cd601118a2`)
và `home-device.png` (SHA-256
`23ec9c5ccb263254f2e877b61abb7932d1c326783b1949aa3602ecc2920a502c`). Hai ảnh
này đã được xem trực tiếp; manifest package vẫn ghi nhận phạm vi host-only, còn
bằng chứng nạp/boot vật lý nằm trong thư mục bằng chứng riêng.

## Kéo-thả và âm thanh hệ thống (2026-11-26)

- `platform/violet/ui/vqeaf_drag.c/.h` là state machine dùng chung: giữ 900 ms,
  vượt 12 px mới tạo ghost, khóa object nhận press, tạm dừng cuộn ở cha và chặn
  gesture của shell. Thả tại chỗ gọi fallback; thả có di chuyển chỉ commit khi
  điểm rơi thuộc đích hợp lệ. Hủy/PRESS_LOST khôi phục opacity, cờ và ghost;
  ghost bị xoá ngoài hệ thống được phát hiện trước khi dùng lại con trỏ.
- Dock: giữ rồi kéo để hoán đổi bốn slot. Home: giữ rồi kéo để sắp xếp lại các
  icon đang thấy trong cùng trang Home. Apps drawer không thêm reorder; giữ tại
  chỗ vẫn mở thông tin. Chế độ “Di chuyển” chạm-chọn slot cũ vẫn được giữ.
- Control Center: ở chế độ Sửa, giữ rồi kéo để đổi thứ tự ô hiển thị. Đặt lại
  khôi phục thứ tự mặc định và hiển thị mặc định. Rebuild được hoãn 30 ms để
  không xoá widget trong lúc LVGL đang phân phối sự kiện.
- Thứ tự dock/Home/Control chỉ nằm trong RAM; reboot về mặc định.

### Clip âm thanh

Tám clip trong `assets/sounds/` được tạo cục bộ bằng exporter của
`D:\Program\webs\8bit-composer` (`OfflineAudioContext`, stereo PCM16 44,1 kHz),
sau đó cắt khoảng lặng, về mono PCM16 22,05 kHz, chuẩn hoá peak 62% và fade đầu/
cuối. Không gọi API ngoài. `scripts/embed_os_sounds.py` kiểm tra định dạng rồi
sinh `platform/violet/ui/sounds/vqeaf_sound_data.c/.h`; WAV và mẫu C phải trùng
từng byte trong gate.

| Sự kiện | Tệp | Khung | SHA-256 |
| --- | --- | ---: | --- |
| Tap | `qeaf-tap.wav` | 5.028 | `b10d4f5c4ec1c18f7cbf1bc5eecdd360c61cb11d87b2e5411cb1755d367b91d7` |
| Drop | `qeaf-drop.wav` | 10.538 | `1ac2e8426d8df35a78d2ecfc4a7fc5dd82ba8fc9c756df95669990001b616c81` |
| Navigate | `qeaf-navigate.wav` | 10.532 | `a1d6103568f5757178b5d8e170be8514ab622134d981363869d66d489b5b6f88` |
| Notify | `qeaf-notify.wav` | 16.034 | `c8998544c97e15ff68eaba5d8c0690f49b302ef3d13d834062e30ac2062da88f` |
| Capture | `qeaf-capture.wav` | 7.778 | `b02e91368f0c6565527fed614ab967352a048ab23d329772f69f52b93d981442` |
| Record | `qeaf-record.wav` | 16.067 | `e334144178ddb9a9df3f547a09a6b2c420a37b8c8d06e47c35025cc894bb216a` |
| Warning | `qeaf-warning.wav` | 21.522 | `7bf9b95947e606d31d145bb74e23164d2e5d7c8e065545f58be387e5c1e2fae5` |
| Charge | `qeaf-charge.wav` | 21.561 | `e4382beca1454638c4e02f14d180d73759b7b69af0923af1cbdcc72d2625d4fd` |

### Backend và sự kiện thật

- `vqeaf_sound.c` dùng OSS `/dev/dsp` trên Linux: mono S16_LE 22,05 kHz, ghi
  nonblocking và tiến trình bằng timer LVGL. Backend chỉ READY sau khi mở, cấu
  hình và ghi hai mẫu im lặng thành công; lỗi mở/ghi/timeout chuyển về trạng thái
  không phát được. Host không có backend thật và báo “Chưa có thiết bị PCM”.
- Âm lượng thật điều khiển hệ số scale; bằng 0 không enqueue. Hàng đợi tối đa
  16 sự kiện. Control Center hiện trạng thái PCM thật và làm mới nhãn khi backend
  lỗi lúc chạy, không ghi “chưa có codec” khi backend đã sẵn sàng.
- Điều hướng, thả/reorder, thông báo, chụp màn hình, bắt đầu/lưu ghi âm, sạc,
  cảnh báo lỗi và phím âm lượng đã nối với sự kiện tương ứng. Thông báo vẫn được
  ghi log khi không phát được tiếng; không mô phỏng thành công nghe được.
- Chưa có bằng chứng PCM/âm thanh phát ra trên Violet vật lý. Việc đăng ký sound
  card không được coi là backend đã chạy; package mới chưa nạp thiết bị.

### Kiểm chứng host/build

- `python scripts/test_violet_shell.py`: PASS `ALL OK`, 77 snapshot, gồm kéo dock,
  hủy ngoài dock, reorder Home và reorder Control; có thêm regression xóa ghost
  ngoài hệ thống và cập nhật nhãn PCM khi backend lỗi lúc chạy; log
  `build/final-shell.log`.
- `python scripts/test_vqeaf_sound.py`: PASS 10 test/26.021 kiểm tra; tám clip C
  trùng từng byte với WAV. Đây là test backend giả, không phải âm thanh vật lý;
  log `build/final-sound.log`.
- `python scripts/render_phone_ui.py`: PASS 216 PNG; log `build/final-render.log`.
- `python scripts/build_violet.py`: PASS 292 object, `/init` 12.625.040 byte,
  SHA-256 `d2e0ec3fbc3bf041dbfb669a7c31df6654805bd5235b4bd06f3c4258c42b6a35`;
  log `build/final-violet-build.log`.
- `python scripts/build_mt689x.py`: PASS 285 object, `/init` 12.395.368 byte,
  SHA-256 `421a9db2fe2fbbfe65f96bcdb9ee1edc3bec2bbf3c080fd9869c2450834a8f62`;
  log `build/final-mt689x-build.log`. Đây chỉ là bằng chứng build MT689x.
- `python scripts/package_violet.py --skip-build`: PASS `validate_bootimg` và
  `audit_rescue_channel`; image 51.666.944 byte, SHA-256
  `450b9f46c9429f9b4f0cae07f03afe6c29ce2898841c9d9203b084a4aaa63a92`;
  log `build/final-violet-package.log`.
- `python scripts/package_violet.py --verify-only`: PASS cả hai gate; manifest
  xác nhận `/init` static AArch64, kernel trùng stock reference và
  `physical_boot_tested=false`; log `build/final-violet-verify.log`.
  Không flash, không ghi `p76/boot`, `userdata`, `misc` và không commit.

## Terminal theo ảnh tham chiếu (2026-11-26)

- Trang Terminal là console đen toàn màn hình (`#121214`), độc lập với
  wallpaper/theme: header có nút drawer tròn bên trái và tiêu đề phiên `~`
  căn giữa; vùng console cuộn độc lập.
- Dòng nhắc lệnh kiểu Termux/fish: `vqeaf@violet` màu teal `#34D8B2`, `~>`
  màu hổ phách `#FFB74D`, lệnh màu sáng; nhãn dùng recolor của LVGL, ký tự
  `#` do người dùng gõ được thay bằng `_` trước khi echo. Lời chào kiểu
  "friendly interactive shell" với `help` được tô teal.
- Keybar phụ trợ gồm 10 phím dạng pill trên dải tối (`#0A0A0C`, phím
  `#2C2C31`), nằm ngay phía trên bàn phím IME khi IME trượt lên: refresh
  (TAB-complete), → (ENTER), ^ (Ctrl+C), `-`, `/`, `.`, và bốn phím mũi tên
  Material Icons (◀ ▲ ▼ ▶: dịch con trỏ trái/phải, lịch sử lệnh lên/xuống).
  Font label là Ubuntu; phím icon dùng bộ icon 28 px đã nhúng.
- Dòng nhập inline: tiền tố `~>` hổ phách + textarea không viền cùng nền
  console; placeholder `help`. Vòng đời IME, lịch sử lệnh và các lệnh
  (help/status/ls/cat/...) giữ nguyên cơ chế cũ.
- Kiểm host: `render_phone_ui.py` render trang `terminal` cho cả 2 profile ×
  2 hướng (50 frame, 200 PNG); `test_violet_shell.py` ALL OK (73 snapshot);
  `test_ime.py` ALL OK; `build_violet.py` PASS, `/init` 12.278.552 byte,
  288 object.

## Biểu tượng hệ thống VPEPixel (2026-11-24)

- 29 tệp `.vpe` trong `assets/icons/vqeaf_os_pixel/` là nguồn artwork gốc, chỉnh
  sửa được. Mỗi tệp là 32×32 RGB565, 2.064 byte và không chứa pixel `0xFFFF`
  (màu trắng dùng làm khóa trong suốt trong VPEPixel). `_sheet.png` là contact
  sheet xem trước, không được nhúng vào ảnh thiết bị.
- `scripts/make_vqeaf_pixel_icons.py --redraw` vẽ lại artwork; chế độ mặc định
  xuất `platform/violet/ui/icons/vqeaf_app_icons.c`; `--check` xác nhận C khớp
  từng byte với dữ liệu `.vpe`. Generator kiểm tra thứ tự enum, kích thước, khóa
  transparency và độ dài payload trước khi xuất.
- `vqeaf_app_icon()` trả descriptor cho 29 ID hợp lệ và `NULL` ngoài dải để các
  mặt tiêu dùng giữ glyph fallback. `vqeaf_icon_img()` chỉ tắt khử răng cưa cho
  ảnh RGB565 32×32; icon QEAPP ARGB8888 và ảnh người dùng không bị ảnh hưởng.
- Bo góc icon: `vqeaf_icon_img()` đặt bán kính **10 px** cho ảnh bitmap dùng
  chung, áp dụng đồng bộ ở Home, Apps, dock và các mặt tiêu khác. Nearest-neighbor
  của pixel art vẫn được giữ; regression có snapshot `home-icon-corners`.
- Kiểm host: `python -B scripts/make_vqeaf_pixel_icons.py --check` PASS;
  `python scripts/test_violet_shell.py` PASS (`ALL OK`, 64 snapshot), gồm ba kiểm
  descriptor/độc nhất/giới hạn ID. Đây là bằng chứng host LVGL, chưa phải chạy
  trên thiết bị vật lý.

## Trạng thái triển khai — 2026-11-11

- LVGL 9.4.0 đã được nối vào vòng đời PID 1 Violet; splash chuyển sang phone shell.
- Có Lock screen, Home launcher, danh sách ứng dụng 24 mục, Control Center,
  Settings, status bar và bottom dock.
- Bottom dock có bốn slot ứng dụng dàn đều với plate icon + nhãn; app đang mở
  có viền accent. Giữ rồi kéo một icon **trong dock** để hoán đổi với slot đích;
  thả ngoài dock thì hủy và giữ nguyên thứ tự. Không kéo icon từ Home/Apps vào
  dock và không có thao tác thả ra ngoài để xoá slot. Giữ tại chỗ mở thông tin;
  “Di chuyển” trong hộp thông tin vẫn cho phép chạm slot đích. Trạng thái dock
  nằm trong RAM và reboot về bốn app mặc định Trình duyệt, Máy ảnh, Thư viện,
  Nhạc. Các màn hình hệ thống vẫn mở được từ home bar, status bar, lưới ứng dụng
  và Cài đặt. Thanh home bar (134/390 W × 5/844 H, chạm để về Home) nằm dưới dock.
  Số đo dọc lấy từ `homo/framework/ui/ios_metrics.c` (dock cao 82/844 H, lề 8/390 W,
  bán kính 39% chiều cao dock); ngang giữ bộ số iPad-style 86 px / lề 10 / bán kính 25.
- Trang **Bàn phím** mở từ Settings: hero icon, trạng thái bố cục, nhóm “Nhập liệu”
  (ngôn ngữ, nhập liệu, nhấn phím) và nhóm “Bàn phím” (giao diện, bố cục, hàng &
  thanh). Chỉ “Giao diện” mở được; các mục chưa có backend hiển thị dấu · và không
  điều hướng, đúng quy ước của Settings.
- Trang **Chủ đề bàn phím**: breadcrumb Trang chủ › Giao diện › Chủ đề, sáu
  card chủ đề dựng sẵn (Nightfall, Pastel, Gradients, Bubble, Facet, Frost) với
  bàn phím thu nhỏ vẽ bằng hình khối, dải màu, nhãn phụ và hàng thao tác. Tất cả
  sáu card có cùng cấu trúc nên lưới thẳng hàng (Bubble/Facet từng thiếu dải màu
  và nhãn phụ); breadcrumb nằm gọn trong canvas, không cụt mép phải. Chạm card
  chỉ **xem trước** (viền accent + dấu check); nút **Áp dụng** commit chủ đề
  cho mọi bàn phím `lv_keyboard` thật của shell — ô tìm kiếm, trang Ghi chú và
  ô mật khẩu Wi-Fi. Nền, phím, chữ (tự chọn sáng/tối theo độ sáng phím), bo
  góc, viền và màu nhấn đều đổi; bàn phím đang mở được tô lại ngay, bàn phím
  mở sau đó kế thừa luôn. Dòng "Đang dùng: ... · Có hiệu lực trong phiên này"
  phân biệt rõ chủ đề đã áp dụng với card đang chọn. Đây là chủ đề của widget
  bàn phím trong shell, không phải IME toàn hệ thống; state nằm trong RAM nên
  khởi động lại về Nightfall — trang ghi rõ giới hạn này.
- Trang **Wi-Fi** có mục **Mạng khả dụng**: quét WEXT trên netdev thật, liệt kê
  SSID/BSSID/mức tín hiệu/khoá, và **Quét lại**. Chạm mạng bảo mật mở modal nhập
  mật khẩu với bàn phím LVGL; mạng mở gọi kết nối trực tiếp. Khi ảnh không có
  netdev/supplicant, modal vẫn nhận mật khẩu nhưng lệnh kết nối trả lỗi thật
  (ENODEV/EOPNOTSUPP) và hiện trong modal, không báo thành công giả.
- **Trung tâm bảo mật** giữ thiết lập PIN và hiển thị vân tay, khoá khi bỏ túi và
  rung phản hồi là chưa hỗ trợ. Hai công cụ thật mở từ hub này:
  - **Kiểm tra Wi-Fi** gọi `vqeaf_wifi_scan()` hiện có để quét thụ động. Danh
    sách chỉ chứa SSID, BSSID, tín hiệu, encryption quảng bá và nhãn đánh giá
    phòng vệ. Mạng mở/WEP cảnh báo; WPA/WPA+WPA2 yêu cầu rà soát; WPA2-PSK được
    ghi là WPA2, không suy ra độ mạnh mật khẩu. **Quét lại** gọi lại provider.
    Không có netdev, driver hoặc firmware thì hiển thị trạng thái và lỗi thật,
    không tạo mạng mẫu trên thiết bị. Không có thao tác kết nối, monitor mode,
    WPS, deauth, bắt mật khẩu hoặc dừng quét.
  - **Cơ sở dữ liệu lỗ hổng** chứa tám advisory CVE công khai thật trong ảnh,
    không cần mạng. Tìm kiếm không phân biệt hoa thường; tối đa bốn từ và mọi
    từ phải khớp. Kết quả mở trang chi tiết với tiêu đề, sản phẩm, ngày công
    bố, mức độ, nguồn, tóm tắt, biện pháp khắc phục và ngày cập nhật catalog.
    Không có payload, lệnh khai thác, thực thi hay cập nhật trực tuyến.
- **Giới hạn bảo mật:** quét Wi-Fi chỉ quan sát metadata cục bộ; encryption
  quảng bá không chứng minh mật khẩu mạnh, WPS an toàn, mạng không bị chiếm
  quyền hoặc không có lỗ hổng. Danh sách CVE là catalog có chọn lọc, không đầy
  đủ và không thay thế nguồn cập nhật của nhà cung cấp. Kết quả chỉ hiện trên
  màn hình, không được lưu. Xem threat model và bằng chứng kiểm thử ở mục đầu
  `docs/STATUS.md` ngày 2026-11-24.
- **QEAPP**: file manager nhận tệp `.qeapp` trong tab QEAFRESCUE; Settings có
  hàng **Cài đặt .qeapp** riêng. Xem trước, cài/cập nhật theo `manifest.id`,
  liệt kê gần đây và gỡ đều đi qua API C công khai v1 không phụ thuộc LVGL.
  Bước xác nhận gắn SHA-256 của đúng gói đã xem trước; nếu tệp bị thay trước
  khi cài, API từ chối và không đổi registry. Ứng dụng cài xong nằm ở Ứng dụng
  → **Đã cài**. v1 chạy tập lệnh `app`/`print`; v2 có module `.qen` native
  (Violet hiện yêu cầu arm64), chỉ được kiểm tra cấu trúc khi xem trước và chạy
  qua device runner đã xác thực. Host renderer không có backend kho `.qeapp`
  nên trả `UNSUPPORTED`, không giả lập cài đặt. Khi thao tác RAM thành công
  nhưng lưu kho p77 thất bại, UI ghi rõ “trong phiên hiện tại” và cảnh báo mất
  sau reboot. Kho p77 dùng `manifest.id` làm khóa, giữ đúng byte đã xác thực
  để các lần lưu sau không đọc lại đường dẫn FAT có thể bị thay. Chữ ký
  HMAC-SHA256 dev key chỉ phát hiện thay đổi, không xác thực nhà phát hành;
  xem docs/QEAPP_FORMAT.md.
- Ứng dụng **Tệp** (mở từ lưới ứng dụng) là file manager chỉ đọc: tab
  **Hệ thống** duyệt rootfs thật (`vqeaf_files_list`, thư mục trước, có `..`),
  tab **QEAFRESCUE** duyệt cả thư mục con của volume FAT16 qua hook đồng bộ nhận
  đường dẫn (`fat16_list_path`/`fat16_read_path`; cluster 0 là gốc). Cả hai tab
  vào thư mục con bằng chạm và đi lên bằng `..`/nút back; tab QEAFRESCUE tự thêm
  `..` vì FAT16 không liệt kê entry chấm. Đường dẫn hiện ở thanh path
  (`QEAFRESCUE /Apps/Tools`). Tệp PC thả vào ổ hiện ở tab QEAFRESCUE sau khi
  Làm mới/Đồng bộ; tệp OS ghi hiện trên PC.
- **Viewer tệp**: chạm tệp mở modal xem trước. Văn bản/JSON/Markdown/log hiện
  nội dung; RAW hiện hex; PNG/JPEG/BMP giải mã thật bằng lodepng/tjpgd (bật
  `LV_USE_LODEPNG`/`LV_USE_TJPGD` trong `lv_conf.h`) và hiện ảnh kèm kích thước;
  GIF/MP3/WAV/MP4/PDF/ZIP chỉ hiện metadata trung thực (`vqeaf_media_inspect`)
  kèm ghi chú rõ là ảnh initramfs chưa có codec phát hay trình kết xuất PDF.
- **Nhấn mở / nhấn giữ**: mọi ô ứng dụng (lưới Home, trang Ứng dụng, dock,
  QEAPP đã cài) mở bằng chạm ngắn; nhấn giữ 900 ms mở hộp thoại thông tin ứng
  dụng (tên, loại, phiên bản, quyền, nút Mở/Di chuyển/Thêm vào dock). Handler
  mở app nghe `LV_EVENT_SHORT_CLICKED` vì LVGL vẫn gửi CLICKED sau long-press.
- Trang **Tệp & đồng bộ** liệt kê thư mục gốc của volume FAT16 QEAFRESCUE qua
  hook có khoá volume (main sở hữu `volume_lock`), kèm nút Đồng bộ. PC thả tệp
  vào ổ thì OS thấy sau khi đồng bộ; tệp OS ghi hiện trên PC ngay — cùng một
  ảnh FAT16 trong RAM, không có bản sao thứ hai.
- Trang **Màn hình khóa** là editor theo ảnh tham chiếu: preview đồng hồ, 4 kiểu
  font (tái dùng font Ubuntu đã nhúng), thanh độ đậm (opacity), 7 màu đồng hồ,
  nút Xong. Lựa chọn áp dụng ngay cho màn hình khóa trong phiên, chưa ghi prefs.
- Trang **Wi-Fi** mở từ Settings: đọc thật `/sys/class/net/wlan*` (operstate,
  MAC), `/proc/net/wireless` (link quality), `/sys/module/wlan` + `cfg80211`, và
  dò blob firmware WCN3990 trong các thư mục firmware chuẩn. Không có netdev thì
  hiện “Không có giao diện mạng”; không có firmware thì hiện “Thiếu” và giải
  thích vì sao không scan/kết nối được. Không bịa SSID, cường độ hay mạng xung
  quanh.
- Trang **Trình điều khiển** (Settings → sau “Bộ nhớ”): trạng thái thật của DRM
  card0, số event evdev, UDC USB gadget, đèn nền, pin và Wi-Fi; mục **Ổ đĩa**
  hiển thị dung lượng root initramfs và LUN QEAFRESCUE 32 MiB, cùng quy tắc chỉ
  ghi p77/recovery.
- Tham chiếu HakiOS (`github.com/oosexgang/hakios-xnu-kernel`): tách rõ tầng
  bring-up (driver + firmware lấy từ chính máy) khỏi tầng Wi-Fi stack/supplicant
  theo `docs/driver-review/wifi-stack.md`, và mô hình chẩn đoán driver/block
  service theo `docs/driver-review/storage.md`. Không chép mã XNU/APSL; chỉ dùng
  cách phân tầng và nguyên tắc “firmware không đi kèm ảnh”. Trạng thái driver
  được đưa vào UI thay vì tuyên bố Wi-Fi hoạt động khi thiếu firmware.
- Hai trang chi tiết mới mở từ Settings:
  - **Lưu trữ** — dung lượng phân vùng gốc từ `statvfs("/")` và RAM từ
    `/proc/meminfo`, kèm thanh tỉ lệ đổi màu khi gần đầy.
  - **Giới thiệu** — tên/phiên bản QEAFIVELS OS, nền tảng, độ phân giải hiện tại,
    thời gian hoạt động, RAM, lưu trữ, độ sáng, pin và Wi-Fi.
  Mọi giá trị đọc từ provider thật; mục không có backend hiển thị “Chưa có dịch
  vụ” và không mở được trang nào.
- Cử chỉ (single-pointer gestures): vuốt lên ở lock screen để mở khóa, vuốt xuống
  trên chrome để mở Control Center, vuốt ngang để chuyển giữa bốn trang nav hoặc
  quay lại từ trang chi tiết. Vuốt dọc trong trang cuộn vẫn là cuộn.
- Harness hiển thị: `python scripts/render_phone_ui.py` dựng chính các nguồn
  `platform/violet/ui` bằng LVGL host (zig, x86_64-windows-gnu) với provider giả
  lập cố định và xuất 60 PNG cho 15 trang × 2 profile × 2 hướng màn hình vào
  `build/screens/phone-ui/`. Script fail nếu ảnh trống, sai kích thước hoặc hai
  trang render trùng nhau. Chỉ `vqeaf_state.c` được thay bằng harness.
- Sửa `scripts/generate_lvgl_fonts.ps1`: `ofs_y` của glyph trước đây tính từ đỉnh
  bitmap nên mọi chữ bị đẩy lên cao, dấu phẩy/tone mark tiếng Việt nằm trên chữ;
  nay tính từ baseline. Toàn bộ font đã được tạo lại.
- Build ARM thành công: `build/violet/vqeaf-init`, khoảng 6.4 MB, 235 object.
- Render gate thành công: 720×1560 và 1560×720, 15 trang mỗi hướng.
- Đây là xác nhận build/render, chưa phải xác nhận giao diện đã chạy trên máy.
  Không flash hoặc ghi phân vùng thiết bị trong bước này.
- Tap cứu hộ giữ contact 3 poll (`violet_tap.h`) và các container `vqeaf_obj`
  không còn mang cờ CLICKABLE mặc định của `lv_obj_create`; nhờ vậy chạm vào
  chữ/khung của một hàng Settings, icon dock hay card chủ đề đều tới đúng handler
  thay vì bị lớp trong suốt nuốt mất. Long-press của dock/app nâng lên 900 ms để
  tap 3 poll không vô tình kích hoạt kéo-thả.

## Giao diện Windows 11 (Fluent) (2026-11-22)

Cài đặt → **Windows 11** bật skin Fluent cho shell trong phiên hiện tại; tắt thì
quay lại giao diện VQEAF. Skin này độc lập với Sáng/Tối và dùng chung một switch
với Liquid Glass — bật cái này sẽ tắt cái kia, vì hai bề mặt không thể cùng lúc.

- **Bảng màu Mica:** nền #F3F3F3 / #202020, surface #FBFBFB / #2B2B2B, accent
  #0067C0 / #4CC2FF; đỏ và hổ phách vẫn mang nghĩa cảnh báo. Bật skin sẽ xoá seed
  Màu động (Material You) để palette không bị nửa Fluent nửa Material.
- **Góc bo:** card dùng `vqeaf_theme_card_radius()` — 12 px (8 dp trên canvas
  720) khi bật, 32 px cho card stock Cobalt Glass khi tắt; app tile 104 px bo 20
  thay vì 30.
- **Bề mặt acrylic:** `vqeaf_acrylic_surface()` tô surface bằng alpha, viền hairline
  và bóng đổ ngắn. Renderer không có backdrop blur nên đây là mô phỏng bằng alpha,
  không phải kính thật.
- **Trang chủ = Start:** lời chào theo giờ thật (`vqeaf_state.hour`), đồng hồ và
  ngày, nút nguồn acrylic mở Control Center, ô tìm kiếm bo 12, mục **Đã ghim**
  dùng lưới app 8 mục, card **Tiện ích hệ thống** (uptime/RAM/lưu trữ thật).
  Dock của shell đổi thành **taskbar**: một dải acrylic liền thay vì icon trôi
  trên hình nền; vẫn có bốn slot, giữ rồi kéo để đổi chỗ và giữ tại chỗ để mở
  sheet thông tin, không dựng hàng launcher thứ hai.
- Trạng thái skin nằm trong RAM initramfs nên khởi động lại trở về giao diện VQEAF;
  trang Cài đặt không tuyên bố đây là thiết lập bền.
- Ảnh đối chiếu: `scripts/render_phone_ui.py` render thêm bốn khung
  `*-fluent` (`lock`, `home`, `apps`, `settings`) cạnh bản gốc.

## Bộ thành phần tương tác (2026-11-23)

Cài đặt → **Bộ thành phần QEAF** có thêm bốn thành phần nằm dưới danh sách M3.
Chúng là demo thành phần trên trang tham chiếu, nên nhãn ghi rõ "demo" và không
ngụ ý có dịch vụ mạng hay tệp phía sau.

- **Paper Plane** — nút gửi 64 px dùng glyph SEND; chạm để chạy hoạt cảnh bay
  lên phải, mờ dần rồi trả glyph về chỗ cũ. Chỉ là chuyển động, không gửi dữ
  liệu đi đâu.
- **Swipe Confirm** — kéo núm tròn hết sang phải để xác nhận; trạng thái xác
  nhận là state thật của widget, chạm lại để đặt lại. Không gắn với hành động
  phá huỷ nào.
- **Download Ring** — vòng cung 270° với nhãn phần trăm; nút **Chạy** mô phỏng
  0 → 100% trong 2,6 giây. Không tải tệp.
- **Checklist** — bốn mục bấm được với checkbox vuông; bộ đếm cập nhật theo
  state trong phiên.

API dùng lại được nằm trong `vqeaf_theme.h`: `vqeaf_m3_paper_plane`,
`vqeaf_m3_swipe_confirm`/`vqeaf_m3_swipe_confirmed`,
`vqeaf_m3_download_ring`/`vqeaf_m3_download_ring_set`, và
`vqeaf_m3_checklist`/`vqeaf_m3_checklist_done`. Mọi trạng thái chỉ sống trong
RAM initramfs và mất khi khởi động lại.

## Liquid Glass tương tác (2026-11-26)

Trang **Thiết kế QEAF** giữ ID và điều hướng cũ, bổ sung showcase 14 thành phần
dùng chung từ `platform/violet/ui/vqeaf_components.c/.h`:

- **Button** phình dưới con trỏ; **Action Button** làm mũi tên co/mờ/tan vĩnh viễn.
- **Tabs** đưa lens chọn từ tab cũ sang tab mới; **Badge** chữ trượt vào và giãn
  chữ từ 3 px về 0.
- **Slide to Confirm** kéo núm kính thật đến cuối; **Card** giữ nội dung với rim
  kính; **Slider** kéo theo pointer, thumb giãn ngang và làm cong track.
- **Switch** phình/trong suốt khi đi qua rồi về trắng; **Checkbox** luôn là kính,
  dấu check nằm dưới cap trong suốt và cap phình khi nhấn.
- **Select** tạo neck giữa nút và danh sách; **Button Group** nhiều nút dùng chung
  một thân kính; **Dropdown Menu**, **Popover** và **Menu** mở bề mặt từ nút gốc,
  menu bên phải/trái theo tham số `side`.

API công khai có constructor/getter/setter và `vqeaf_lg_overlay_open/close` cho
Select/Dropdown/Popover/Menu. Hành động phát `LV_EVENT_CLICKED`; control có state
phát `LV_EVENT_VALUE_CHANGED`. Chuỗi item được sao chép sâu, tối đa
`VQEAF_LG_MAX_ITEMS` là 64. Constructor trả `NULL` khi parent/danh sách/count không
hợp lệ hoặc cấp phát/object/event thất bại, đồng thời dọn cây tạo dở.

Overlay gồm scrim, neck và panel là ba anh em trên `lv_layer_top()`. Context có
thế hệ, hủy async được cancel, callback người dùng được kiểm tra lại sau reentrancy;
xoá anchor, thay overlay hoặc xoá scrim từ bên ngoài không để lại con trỏ đã giải
phóng. Khi overlay mở, gesture của shell bị chặn bởi `vqeaf_lg_any_overlay_visible()`.

**Giới hạn hình ảnh:** renderer chưa có backdrop blur/refraction vật lý. Kính được
mô phỏng bằng gradient có clip, alpha, scale, translation, rim và shadow; nội dung
phía sau vẫn có thể nhìn xuyên qua. Demo không gắn các control này vào dịch vụ mạng,
tệp hoặc trạng thái phần cứng giả.

Kiểm chứng host/build cho bản này:

- `python scripts/test_m3.py`: **PASS 139/139**; log
  `build/liquid-hardening-m3.log`. Có quét lỗi allocation/object/event, reentrancy,
  async hủy/thay thế/lỗi cấp phát, object sai kiểu, bản sao chuỗi và geometry 8×8.
- `python scripts/test_violet_shell.py`: **PASS 478/478**, 73 snapshot; log
  `build/liquid-hardening-shell.log`. Pointer indev LVGL thật kéo slide/slider, bấm
  switch/checkbox/tab, chọn item Select và đóng bằng backdrop.
- `python scripts/render_phone_ui.py`: **PASS**, 216 PNG cho 720×1560/1560×720 với
  hai profile provider `unknown`/`providers`; log `build/liquid-hardening-render.log`.
- `ctest --test-dir build-native --output-on-failure`: **PASS 32/32**; log
  `build/liquid-hardening-ctest.log`.
- `python scripts/build_violet.py`: **PASS**, `/init` static AArch64 12.347.512
  byte, 289 object, SHA-256
  `a2088ba7568d72e6fc714a586dded1591c0eeb324cb6d8e68ff1bf432dd980d1`.
- `python scripts/build_mt689x.py`: **PASS**, `/init` static AArch64 12.128.232
  byte, 282 object, SHA-256
  `b65ed1850f6dc9d8b076495a108ee8ff7e2fd33bfddb2c2110dc8c6087eaacd1`;
  log `build/liquid-hardening-mt689x-build.log`. Chưa boot MT689x trên thiết bị.
- `python scripts/package_violet.py --skip-build` và `--verify-only`: **PASS**.
  Image 51.519.488 byte, SHA-256
  `9111a1554bf7257b4da237eb7fb3a12473c5414e0ae1a06c0b809e737c74aa48`;
  `validate_bootimg` và `audit_rescue_channel` đều PASS. Kernel payload 17.505.611
  byte vẫn trùng stock reference
  `6d298f543cfee08f7b72687cf975b6ed232e4ed565d337156db39ceb0fc18513`.
- **Chưa có bằng chứng vật lý:** manifest ghi `physical_boot_tested=false`; chưa nạp
  p77, không ghi `p76/boot`, `userdata`, `misc` và chưa commit.

## Trung tâm điều khiển Midnight V2 (2026-11-24)

Trung tâm điều khiển được dựng lại theo nhịp hoạt ảnh của video tham chiếu,
nhưng giữ bảng màu và ngôn ngữ thiết kế VQEAF Midnight V2. Đây là overlay
toàn màn hình phủ lên trang đang mở; dock, home bar và navigation bị ẩn trong
phiên overlay, còn status bar được đưa lên trên. Khi đóng, shell khôi phục
đúng trang trước đó thay vì luôn quay về Home.

- Mở: dịch từ `-1/5` chiều cao màn hình về `0`, opacity `0 → 255`; header,
  hai pill hành động, lưới và slider lần lượt hiện với delay. Thẻ phương tiện
  chỉ tham gia chuỗi hiện này khi có phiên phát thật.
- Đóng: dịch lên và mờ dần. Vuốt lên được nhận ở `LV_EVENT_SCROLL_BEGIN` của
  panel rồi đọc vector đầu vào; LVGL không gửi gesture khi scroll object đang
  sở hữu thao tác kéo. Yêu cầu đóng giữa lúc mở sẽ huỷ animation cũ trước
  khi bắt đầu animation đóng.
- Bảng thu gọn có 10 ô vuông bo tròn 5×2; bảng mở rộng có 15 ô. Ở chế độ Sửa,
  giữ rồi kéo giữa các ô đang hiển thị để đổi thứ tự; huy hiệu trừ bỏ ô khỏi bảng
  thu gọn, còn Đặt lại khôi phục cả thứ tự mặc định lẫn hiển thị. Thứ tự được lưu
  riêng với tập ô thu gọn. Thay đổi chỉ sống trong RAM của phiên.
- Hai pill rộng gọi chụp/quay màn hình thật. Khi không có phiên phát, Control
  không dựng thẻ phương tiện và vẫn giữ hai slider. Backend phát thật phải gọi
  `vqeaf_ui_media_session_start()` sau khi có đầu ra, rồi gọi
  `vqeaf_ui_media_session_stop()` khi tạm
  dừng, kết thúc, lỗi hoặc teardown. Đọc metadata, chọn bài, mở Music, chỉnh
  hàng đợi và giải mã khung QVF tĩnh không tạo phiên phát. Thẻ hiển thị loại,
  tiêu đề, chi tiết và trạng thái “Đang phát” từ phiên thật; không có nút
  previous/play/next hay “Mở Music” giả. Độ sáng ghi vào provider đèn nền khi
  có; âm lượng áp dụng cho clip PCM khi backend thật sẵn sàng, còn dòng trạng thái
  dưới slider cho biết backend đã mở/cấu hình/ghi được hay chưa.
- Wi-Fi/Bluetooth đọc provider. Dữ liệu, đèn pin, GNSS, hotspot, chế độ, máy bay
  và các chức năng chưa có backend hiện “Không hỗ trợ”; chạm vào giải thích
  giới hạn thay vì bật công tắc giả.
- Rebuild sau thao tác được hoãn bằng timer một lần để không xoá nút đang xử lý
  sự kiện và để tap không lọt xuống trang nền. Vị trí cuộn được giữ lại; phiên
  media bắt đầu/kết thúc làm Control mở lại nội dung mà không đóng overlay.
- Kiểm chứng host: `python scripts/test_violet_shell.py` PASS, 55 snapshot;
  `python scripts/render_phone_ui.py` PASS, 164 PNG cho 720×1560/1560×720 và
  hai profile provider. Ảnh nằm trong `build/screens/phone-ui/`.
- Kiểm chứng vật lý sau khi nạp p77: thiết bị `ca1af25d` boot QEAFIVELS OS
  0.3.0, DRM scanout 1080×2340 và LVGL 720×1560. Ảnh scanout
  `build/violet-control-device-20261124/screen/boot-screen.png` xác nhận trực
  quan header, hai pill, lưới 5×2, thẻ phương tiện và hai slider của bản đã
  nạp trước đó; **không phải bằng chứng cho hành vi thẻ media có điều kiện trong
  gói mới**, vì gói mới chưa được nạp. Ảnh bổ sung `SHOT0001.BMP` 1080×2340
  được tạo sau khi PNG encoder lỗi 83; chi tiết và lỗi metadata host của lần
  chụp này được ghi trong `docs/STATUS.md`.

## Khởi động lại / Tắt nguồn (2026-11-12)

Menu nguồn (nhấn giữ phím nguồn) có ba hành động: **Khởi động lại**,
**Tắt nguồn**, **Ngủ**. Hai hành động đầu không reset máy ngay: chạm xong hiện
màn hình toàn màn hình kiểu Windows với vòng xoay, tiêu đề
"Đang khởi động lại" / "Đang tắt nguồn" và dòng trạng thái theo công việc thật
("Đang đóng các dịch vụ...", "Đang lưu nhật ký hệ thống...",
"Đang đồng bộ dữ liệu...", "Đang chờ phần cứng khởi động lại..."). Không có
thanh phần trăm giả.

`platform/violet/ui/vqeaf_shutdown.{h,c}` chỉ vẽ và điều phối; PID 1
(`main_violet.c`) là nơi duy nhất gọi `reboot()`. Hook ghi từng bước vào
`QEAFBOOT.LOG` trên QEAFRESCUE, cập nhật `status` (`stage=restart`,
`power=restart`), gọi `sync()` rồi `reboot(RB_AUTOBOOT)` hoặc
`reboot(RB_POWER_OFF)`. Nếu kernel từ chối, màn hình báo lỗi thật và có nút
"Quay lại" — không báo khởi động lại thành công giả. Trong lúc chờ reset, mọi
chạm, phím cứng và timer tắt màn hình bị bỏ qua.

Sau reset, firmware nạp lại boot image, splash rồi Lock screen; dock, chủ đề
khoá và uptime về mặc định vì state sống trong RAM initramfs. Đây là cùng
chu trình "Restart → logo → màn hình khoá" của Windows, không phải chỉ dựng lại
UI trong tiến trình đang chạy. Chưa flash hoặc bấm nút này trên máy thật;
bằng chứng hiện tại là test LVGL host + build/đóng gói ảnh.

## Điều hướng bằng vuốt (2026-11-12)

- **Vuốt phải từ Trang chủ → danh sách ứng dụng** (trang "Ứng dụng", ô tìm
  kiếm, lưới 24 app). Trước đây cú vuốt này wrap vòng nav sang Cài đặt.
- Vuốt trái từ Trang chủ vẫn vào danh sách ứng dụng; vuốt phải từ danh sách
  ứng dụng về Trang chủ; vuốt ngang giữa bốn trang nav giữ nguyên thứ tự cũ
  (trái = trang tiếp, phải = trang trước). Từ trang chi tiết, vuốt ngang về
  trang cha tương ứng.
- **Một lần chạm = một lần đổi trang.** Sau khi xử lý gesture, shell gọi
  `lv_indev_wait_release()` trước khi dựng lại nội dung: các poll còn lại của
  cú vuốt bị bỏ qua, nên nhấc tay ra vẫn đứng yên ở trang vừa mở, không đi
  tiếp vài trang rồi vòng về Home, cũng không có cú chạm nhầm vào icon vừa
  được vẽ lại. Lần chạm tiếp theo vẫn nhận bình thường.

## Tắt màn hình & chế độ ngủ (2026-11-12)

Cài đặt → **Tắt màn hình** dùng tiếng Việt có dấu cho cả nhãn lẫn giá trị
(30 giây / 1 phút / 5 phút / 30 phút / Không bao giờ). Chạm vào dòng để đổi;
giá trị hiển thị luôn lấy từ `screen_timeout_s` thật, không hard-code.

Hết thời gian rỗi: 3 giây cuối màn hình **mờ** còn 1/4 độ sáng, rồi backlight
về 0, `screen_off = 1`, shell về **Lock** và vào chế độ ngủ. Khi ngủ, PID 1
giảm vòng lặp từ 16 ms xuống 100 ms và **dừng hẳn `lv_timer_handler()`,
`display_poll()` cùng mọi lần đọc sysfs mỗi giây** — chỉ còn poll phím nguồn và
cảm ứng để thức. Chạm hoặc nhấn phím nguồn khôi phục đúng độ sáng đã lưu và
vẽ lại ngay. Nút **Ngủ** trong menu nguồn dùng chung state machine này.

Trước đây `vqeaf_ui_show()` gọi `vqeaf_ui_activity()` nên chính thao tác về
Lock sau timeout bật đèn lại và xoá cờ ngủ: màn hình không bao giờ tắt. Hai
luồng đã tách: đổi trang chỉ reset idle timer, còn wake chỉ xảy ra khi có input
thật. Kiểm chứng bằng test LVGL host với đồng hồ giả 1 giây; chưa đo mức tiết
kiệm pin trên phần cứng thật.

## Các phần tiếp theo

1. Kiểm thử trên thiết bị (touch, gesture, xoay ngang) với quyền triển khai riêng;
   đối chiếu ảnh render với framebuffer thật.
2. Notification Center có provider thông báo thật; Recent Apps gắn với app runtime.
3. Thêm File Manager, Gallery, Music, Video, Camera và App Store khi có backend
   tương ứng — không dựng trang giả.
4. Smoke test tự động cho navigation/gesture trong harness (mô phỏng pointer),
   sau đó mở rộng sang font tiếng Việt và trạng thái không có phần cứng.
