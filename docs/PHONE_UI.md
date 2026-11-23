# VQEAF OS — thiết kế giao diện LVGL

## Mục tiêu

Giao diện smartphone native cho cổng Violet, dùng LVGL 9.4.0 và lấy bảng thiết kế
`uiux/546546.png` làm tham chiếu trực quan. QeafdropOs là nguồn tham khảo về
cách tổ chức status bar, launcher, app grid, control center và các trang ứng dụng;
không liên kết runtime với QeafdropOs.

## Quy chuẩn giao diện

- **Liquid Glass** là tuỳ chọn giao diện trong phiên: Home, màn hình khóa và
  các bề mặt nổi dùng alpha, gradient, viền sáng và nền hoàng hôn dựng từ vector;
  thẻ Settings giữ nền đặc. Hai hàng biểu tượng Home không có khung bao ngoài.
  Renderer chưa có backdrop blur/refraction, nên kính được mô phỏng bằng alpha
  và gradient. Tùy chọn mất khi khởi động lại.
- Wallpaper OS là cảnh vector vẽ bằng gradient và các hình elip native; không
  tạo `lv_image_dsc_t`, không gọi `lv_image_create()` và không cần image decoder.
- Biểu tượng ứng dụng hệ thống dùng glyph vector thay bitmap nhúng; trình xem
  ảnh/tệp vẫn giữ khả năng hiển thị nội dung do người dùng mở.
- Canvas dọc 720×1560, tương ứng panel 1080×2340; canvas ngang 1560×720.
- Mặc định Light: nền #F2F2F7, surface trắng, chữ #1C1C1E và accent xanh #007AFF;
  Dark vẫn chọn được trong Settings.
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
- `platform/violet/ui/vqeaf_pages.c`: Lock, Home, Apps, Control Center và Settings.
- `platform/violet/ui/vqeaf_theme.c`: palette (kể cả vai trò Material 3), card,
  tile, typography, wallpaper, và design system M3 (shape/elevation/state
  layer/ripple + button/FAB/chip/switch/card).
- `platform/violet/ui/vqeaf_state.c`: clock, battery, Wi-Fi, storage, uptime và
  backlight provider.
- `platform/violet/ui/fonts/`: Ubuntu và Material Icons được biên dịch tĩnh.

## Trạng thái triển khai — 2026-11-11

- LVGL 9.4.0 đã được nối vào vòng đời PID 1 Violet; splash chuyển sang phone shell.
- Có Lock screen, Home launcher, danh sách ứng dụng 24 mục, Control Center,
  Settings, status bar và bottom dock.
- Bottom dock có bốn slot ứng dụng dàn đều với plate icon + nhãn, không có khung
  bao nền/viền; app đang mở có viền accent.
  Dock là nơi chứa ứng dụng hệ thống, không cố định: giữ lâu một icon trong dock
  hoặc trong lưới ứng dụng để nhấc lên, thả vào slot để đổi chỗ/đưa ứng dụng vào
  dock, thả ra ngoài để bỏ khỏi dock (slot trống hiện dấu +). App trùng được gỡ
  khỏi slot cũ nên dock không chứa hai bản sao. Trạng thái dock nằm trong RAM;
  khởi động lại trả về bốn app hệ thống mặc định (Trang chủ, Ứng dụng, Điều khiển,
  Cài đặt) vì root initramfs không bền. Dock mặc định của bản giao diện này là
  bốn ứng dụng Điện thoại, Tin nhắn, Trình duyệt, Thư viện (id 4/5/7/9 trong
  registry); các màn hình hệ thống vẫn mở được từ home bar, status bar, lưới
  ứng dụng và Cài đặt. Thanh home bar (134/390 W × 5/844 H, chạm
  để về Home) nằm dưới dock.
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
- **QEAPP**: file manager nhận tệp `.qeapp` trong tab QEAFRESCUE và hiện nút
  Cài; cài xong ứng dụng nằm ở Ứng dụng → **Đã cài** và mở được trang QEAPP
  (id, version, nội dung `print` từ main.qe, nút Gỡ). Chỉ chạy `app`/`print`,
  không nạp mã native. Chữ ký là HMAC-SHA256 dev key; xem docs/QEAPP_FORMAT.md.
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
  720) khi bật, 28 px như cũ khi tắt; app tile 104 px bo 20 thay vì 30.
- **Bề mặt acrylic:** `vqeaf_acrylic_surface()` tô surface bằng alpha, viền hairline
  và bóng đổ ngắn. Renderer không có backdrop blur nên đây là mô phỏng bằng alpha,
  không phải kính thật.
- **Trang chủ = Start:** lời chào theo giờ thật (`vqeaf_state.hour`), đồng hồ và
  ngày, nút nguồn acrylic mở Control Center, ô tìm kiếm bo 12, mục **Đã ghim**
  dùng lưới app 8 mục, card **Tiện ích hệ thống** (uptime/RAM/lưu trữ thật).
  Dock của shell đổi thành **taskbar**: một dải acrylic liền thay vì icon trôi
  trên hình nền; vẫn là dock kéo-thả cũ (4 slot, nhấn giữ mở sheet thông tin),
  không dựng hàng launcher thứ hai.
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
