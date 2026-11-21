# VQEAF UI — quy trình kiểm thử trên Redmi Note 7 Pro (violet)

Tài liệu này là quy trình đầy đủ để đưa giao diện LVGL trong
`platform/violet/ui/` lên máy thật và đối chiếu với ảnh render trên PC.
Mọi bước đều có gate; bước nào fail thì dừng, không có bước "bỏ qua cho nhanh".
Nạp ảnh lên máy là hành động riêng, cần sự đồng ý rõ ràng (mục 5) — tài liệu
này không tự nạp.

Điều được chứng minh: ảnh build/package đúng cấu trúc, boot lên PID 1, DRM quét
đúng framebuffer, từng trang UI hiện đúng dữ liệu provider và khớp bố cục với
render. Điều **không** được chứng minh bởi quy trình này: driver cảm ứng hoạt
động đúng dưới ngón tay ở mọi điều kiện, backend Wi-Fi/BT/pin, app runtime, hay
tính độc lập khỏi kernel Linux (xem `docs/STATUS.md`).

## 1. Điều kiện tiên quyết

| Thành phần | Yêu cầu |
|---|---|
| Host | Windows + Git Bash, Python 3.10+ |
| Toolchain | Zig 0.13 tại `QEAFDROPOS\out\toolchain\zig-windows-x86_64-0.13.0\zig.exe` (hoặc đặt `ZIG=`) |
| QeafdropOs | `D:\QeafivelsOfficial\QeafdropOs` (hoặc `QEAFDROPOS=`), có kernel dump `out/reference/stock-violet/boot.img` |
| Máy | Redmi Note 7 Pro (`violet`) đang chạy bản VQEAF violet trước đó, USB hoạt động, hiện `QEAFRESCUE` khi cắm |
| Quyền | Chỉ nạp `p77` (recovery). Không bao giờ ghi `p76/boot`, `userdata`, `misc` |

Nếu máy đang chạy hệ điều hành gốc hoặc chưa từng có bản VQEAF violet, quy trình này
dừng ở mục 4: chưa có kênh cứu hộ trên máy để nạp an toàn.

## 2. Gate 1 — build ELF aarch64

```bash
python scripts/build_violet.py
```

PASS khi:

- In ra `build_violet: linked .../build/violet/vqeaf-init (...)`;
- File là ELF64 aarch64 (script tự kiểm tra magic `0x7f ELF`, class 2, machine `0xB7`).

FAIL thường gặp: thiếu `ZIG`, thiếu `platform/lvgl`, hoặc lỗi biên dịch UI. Sửa
nguyên nhân, không sửa gate. Ghi lại số object và kích thước ELF để đối chiếu
với lần render.

## 3. Gate 2 — render baseline trên PC

```bash
python scripts/render_phone_ui.py
```

PASS khi:

- `720x1560 ok (15 pages x 2 profiles)` và `1560x720 ok (15 pages x 2 profiles)`;
- `wrote 60 PNGs` vào `build/screens/phone-ui/`;
- Script tự fail nếu một ảnh trống (dưới 30 màu lấy mẫu), sai kích thước, hoặc
  hai trang cho ra khung hình giống nhau.

Ảnh baseline là 15 trang (`lock home apps control settings storage about keyboard
keyboard-themes wifi drivers sync lock-theme files qeapp`) × 2
profile (`unknown`: không có giờ/pin/Wi-Fi; `providers`: có đủ) × 2 hướng.
Máy thật gần như luôn ở profile `unknown` vì chưa có backend pin/Wi-Fi — dùng
đúng profile đó khi so sánh.

Quan trọng: **render và ELF phải được build từ cùng một cây làm việc trong cùng
buổi**. Render script không chứa mã riêng của giao diện, nhưng nếu sửa UI sau khi
render thì ảnh baseline đã cũ.

## 4. Gate 3 — đóng gói + hai cổng của QeafdropOs

```bash
python scripts/package_violet.py
```

PASS khi:

- `validate_bootimg` PASS (cấu trúc boot image, kernel/ramdisk/địa chỉ/kích thước);
- `audit_rescue_channel` PASS (`QEAF.CMD`, `flash recovery`, marker
  `gadget_configfs`/`UDC`/`init_panel`/`init_gadget` nằm **trong chính ELF `/init`**);
- In ra `size : ... bytes (p77 is 67108864)` và `sha256: ...` của
  `dist/boot-vqeaf-violet.img`.

Ghi vào biên bản: kích thước ELF, số object, kích thước ảnh, SHA-256 ảnh.

Dừng ngay nếu: gate fail, ảnh > 64 MiB, hoặc bạn đang cầm `dist/vqeaf-x86_64-*.img`
— ảnh x86_64 không bao giờ được nạp lên điện thoại.

## 5. Triển khai (chỉ khi có đồng ý rõ ràng)

Bỏ qua mục này nếu chưa được phép nạp máy. Khi được phép, chỉ dùng một trong hai
đường của QeafdropOs, và luôn chạy dry-run trước:

```powershell
# Đường A — máy đang chạy VQEAF, volume QEAFRESCUE là F:
python D:\QeafivelsOfficial\QeafdropOs\scripts\prepare_rescue_flash.py `
    --image dist\boot-vqeaf-violet.img --volume F:            # dry run
python D:\QeafivelsOfficial\QeafdropOs\scripts\prepare_rescue_flash.py `
    --image dist\boot-vqeaf-violet.img --volume F: --commit    # consent
```

```powershell
# Đường B — máy đang ở fastboot (vd. sau khi thoát khỏi bản cũ)
D:\QeafivelsOfficial\QeafdropOs\scripts\restore_p77.ps1 `
    -Image dist\boot-vqeaf-violet.img -Execute -ConfirmPartitionWrite
```

Bằng chứng bắt buộc sau khi ghi (đọc raw sector, không đọc view mount):

- `RESPONSE.TXT`: `flash: written and verified byte for byte`
- `QEAFBOOT.LOG`: `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over N bytes`
  với `N` = kích thước ảnh.

Sau đó `reboot` qua kênh cứu hộ (`send_rescue_cmd.py --volume F: --cmd reboot`).

## 6. Gate 4 — bằng chứng boot và đường UI

Sau reboot, đọc `HOMOLOG.TXT`, `DEVINFO.TXT`, `QEAFBOOT.LOG` bằng raw sector:

```powershell
python D:\QeafivelsOfficial\QeafdropOs\scripts\read_rescue_raw.py `
    --drive F: --out build\screens\device\evidence `
    --files HOMOLOG.TXT DEVINFO.TXT QEAFBOOT.LOG RESPONSE.TXT
```

PASS khi `HOMOLOG.TXT` có đủ:

| Dòng | Ý nghĩa |
|---|---|
| `boot: QEAFIVELS OS 0.3.0 init on violet (PID 1)` | PID 1 của chúng ta đã chạy |
| `usb: mass storage bound to a600000.dwc3` | kênh cứu hộ sống (một dòng `No such device` đầu tiên là UDC chưa đăng ký, retry sẽ bind) |
| `drm: verified - crtc <n> scans our fb <n> at 1080x2340` | DRM quét đúng buffer của mình |
| `ui: splash 720x1560 (portrait)` | canvas dọc đã dựng |
| `ui: LVGL phone shell active at 720x1560` | shell LVGL đã vào vòng lặp |

Dừng và xử lý sự cố (mục 9) nếu thiếu dòng DRM/UI: màn hình có thể vẫn là splash
hoặc tối, nghĩa là giao diện chưa chạy dù máy đã boot.

## 7. Chụp từng trang và đối chiếu với render

Trên máy, `snap` ghi `SCREEN.RAW` = 240×520 RGB565 của scanout thật. Quy trình
cho mỗi trang:

1. Mở trang cần kiểm: bằng ngón tay, hoặc bằng `tap` qua kênh cứu hộ (mục 7b).
2. Gửi lệnh có nonce để chắc chắn là phản hồi mới:
   ```powershell
   python D:\QeafivelsOfficial\QeafdropOs\scripts\send_rescue_cmd.py --volume F: --cmd "echo <nonce>"
   python D:\QeafivelsOfficial\QeafdropOs\scripts\send_rescue_cmd.py --volume F: --cmd "snap"
   ```
3. Flush volume rồi đọc `SCREEN.RAW` bằng raw sector:
   ```powershell
   Write-VolumeCache F
   python D:\QeafivelsOfficial\QeafdropOs\scripts\read_rescue_raw.py `
       --drive F: --out build\screens\device --files SCREEN.RAW
   Move-Item build\screens\device\SCREEN.RAW build\screens\device\<page>.raw -Force
   ```
4. So sánh:
   ```bash
   python scripts/ui_snapshot_compare.py --raw build/screens/device/home.raw --page home
   ```
   hoặc cả thư mục (mỗi `<page>.raw`):
   ```bash
   python scripts/ui_snapshot_compare.py --capture-dir build/screens/device
   ```

Công cụ lấy render 720×1560 của đúng trang, hạ mẫu 3:1 về lưới 240×520, lượng tử
qua RGB565 rồi so với capture. Ngưỡng mặc định: ≥ 90% pixel trong sai số 40/channel
và sai số trung bình ≤ 12; đổi bằng `--min-match`, `--tolerance`, `--max-mean`.
Mỗi lần so sánh ghi `build/screens/phone-ui/compare-<page>.png` (trái: render,
phải: máy) để người kiểm xem bằng mắt.

Trước khi có máy, chạy `python scripts/ui_snapshot_compare.py --self-test`: nó
tự tạo một capture từ render (phải PASS) và so capture đó với trang khác (phải
FAIL) — chứng minh gate phân biệt được, không chỉ luôn PASS.

Lưu ý hướng ngang: capture luôn là khung 240×520 của panel, nên khi UI ở
1560×720 ảnh bị xoay. Với ảnh chụp sau `rotate 90`, thêm `--landscape`: công cụ
lấy render 1560×720, xoay 90° về khung panel rồi mới hạ mẫu. Không có cờ này,
công cụ chỉ so với render dọc và sẽ báo lỗi kích thước hoặc điểm khớp thấp.

### 7b. Tự động hoá bằng `tap`

`hold X Y [ms]` giữ một điểm chạm trong `ms` (mặc định 1200, cho phép
100..10000) — đủ để timer nhấn giữ 900 ms của shell bắn `LV_EVENT_LONG_PRESSED`,
nên kiểm được sheet thông tin và mọi hành vi nhấn giữ từ host. Đã dùng để xác
nhận: nhấn giữ icon Terminal trong "Đã cài" mở sheet thông tin, rồi `tap` vào
nút mở trang chi tiết (ảnh `build/screens/device/final-sheet.png`,
`final-info.png`).

Trang app native tự quyết định status bar (render host của nhánh "dải trên
trống": `build/screens/phone-ui/qeapp-native-720x1560-providers.png`): đọc dòng `ui: qeapp frame …` trong `kmsg`/`HOMOLOG.TXT` để biết
khung app có để trống dải trên hay không (`… leaves the top band blank, status
overlaid` / `… paints the top band, status hidden`).

`power` giả lập một lần nhấn nút nguồn ngắn (đúng hàm `vqeaf_ui_toggle_screen()`
mà phím thật gọi): lần đầu tắt màn hình và đưa về màn hình khoá, lần sau đánh
thức tới màn hình khoá. Dùng để kiểm khoá/mở khoá từ host — ví dụ mở app native
rồi `power` → `power` → `swipe` lên đã xác nhận status bar hiện lại ở cả màn
hình khoá và trang chủ (ảnh `build/screens/device/lock-2-locked.png`,
`lock-3-woken.png`, `lock-4-unlocked.png`).

Sheet thông tin nằm ở `lv_layer_top()`; nút của nó chiếm y 866..932 trên canvas
720x1560 (`Đóng` x 211..346, `Thông tin` x 359..508) — chạm ngoài card sẽ đóng
sheet qua lớp scrim. Nếu một cú `tap` "không ăn", chạy `kmsg` (hoặc đọc
`HOMOLOG.TXT`) và tìm dòng `hit X,Y -> x1,y1..x2,y2 kids=.. clickable=..`: nó in
đúng object mà `lv_indev_search_obj()` trả về cho điểm đó, tức là cùng cây mà
LVGL dùng để hit-test.

`tap X Y` chèn một cú chạm tại toạ độ canvas (không phải toạ độ panel thô): main
thread nhận một press ở lần poll kế tiếp và release ở lần sau, nên LVGL xử lý
đúng như ngón tay. `swipe X0 Y0 X1 Y1 [steps]` đi xa hơn: press tại điểm đầu,
mỗi poll nhích một bước (mặc định 8, cho phép 2..64) tới điểm cuối rồi release
— đủ để LVGL cuộn một trang. Cả hai trả `... queued for the UI thread` khi đã
xếp hàng, `... refused` khi điểm ngoài canvas, UI chưa dựng hoặc đang có gesture
chờ; mỗi loại nhận một yêu cầu mỗi lần (yêu cầu thứ hai bị từ chối).

Dock dưới cùng mặc định chứa Điện thoại / Tin nhắn / Trình duyệt / Thư viện,
nên điều hướng test dùng home bar, status bar và vuốt ngang thay vì tap dock.

Toạ độ cho canvas 720×1560:

| Trang | Chuỗi tap |
|---|---|
| Unlock | swipe `(360, 1250)`→`(360, 250)` |
| Home | swipe mở khóa (hoặc tap home bar `(360, 1540)`) |
| Ứng dụng | mở khóa → `(654, 346)` (“Tất cả”) |
| Điều khiển | mở khóa → `(100, 30)` (status bar) |
| Cài đặt | mở khóa → `(100, 30)` → swipe `(600, 1100)`→`(120, 1100)` |
| Lưu trữ | Cài đặt → `(360, 1047)` |
| Wi-Fi | Cài đặt → `(360, 318)` |
| Trình điều khiển | Cài đặt → `(360, 1330)` |
| Tệp & đồng bộ | Cài đặt → swipe dọc `(360, 1200)`→`(360, 300)` hai lần → `(360, 1067)` |
| Màn hình khóa | Cài đặt → swipe dọc hai lần → `(360, 921)` |
| Tệp (file manager) | mở khóa → `(654, 346)` → tap tile “Tệp” `(101, 675)` |
| Giới thiệu | Cài đặt → swipe dọc hai lần → `(360, 1213)` |

Chạy cả loạt — lệnh đầu là dry run, thêm `--run` để thực sự gửi lên máy:

```bash
python scripts/ui_capture_pages.py                 # in kế hoạch
python scripts/ui_capture_pages.py --run           # tap + snap + kéo raw về
python scripts/ui_snapshot_compare.py --capture-dir build/screens/device
```

Swipe `(360,828)→(360,100)` dài 728 px, vượt giới hạn cuộn của trang Cài đặt
(≈707 px ở canvas 720×1560), nên trang cuộn hết và hàng “Giới thiệu” — hàng cuối
của 13 hàng, cao 124 — nằm khoảng y=1300..1424; tap `(360,1360)`.

Script kiểm tra phản hồi `snap: wrote SCREEN.RAW`, kích thước raw đúng 249.600
byte, và dừng khi gặp `... unavailable`/`... refused`. Toạ độ trên chỉ đúng ở
canvas dọc 720×1560; `lock` phải là trang đầu tiên sau boot vì UI không có đường
quay lại lock.

Ba đặc tính của máy thật đã gặp và được script xử lý:

- **Phải ở dọc trước khi chụp.** Sau `rotate auto` máy tự xoay sang 1560×720;
tap theo toạ độ dọc khi đó bị từ chối (`refused (outside the canvas or UI down)`)
và ảnh vẫn là trang cũ. Script đọc `status` và từ chối chạy nếu thấy landscape —
chạy `rotate 0` trước.
- **Khung hình cần lắng.** Một `snap` duy nhất ở 0,8 s trả về trang trước; script
lặp `snap`/đọc tới khi hai lần liên tiếp giống nhau (`--tries`, mặc định 5).
- **Đừng tap ngay sau khi đổi trang.** Tap kế tiếp có thể rơi vào trang cũ khi
LVGL còn dựng trang mới (Storage vì thế vẫn ra Settings). Với chuỗi có hai tap
liên tiếp, dùng `--wait 8`.

## 8. Checklist giao diện

Mỗi mục ghi: PASS/FAIL, ảnh `build/screens/device/<page>.raw`, điểm khớp từ
`ui_snapshot_compare.py`, và ghi chú khác biệt có chủ ý.

| # | Trang / hành vi | Cách mở | Kỳ vọng (đối chiếu render) | PASS khi |
|---|---|---|---|---|
| 1 | Lock | mặc định sau boot | đồng hồ lớn, ngày, chip thời tiết “Chưa có dữ liệu”, nút khóa, “Vuốt để mở khóa” | khớp `lock-720x1560-unknown`; giờ hiển thị `--:--` nếu RTC chưa có năm ≥ 2020 |
| 2 | Mở khóa bằng vuốt | vuốt lên từ lock | sang Home, nav bar hiện | Home hiện đúng; không cần chạm nút |
| 3 | Home | sau mở khóa | 8 tile, dock 4 mục, footer “QEAFIVELS OS · chỉ hiển thị trạng thái thật” | khớp `home-...-unknown`; không có số liệu bịa |
| 4 | Ứng dụng | chạm “Ứng dụng” trên nav | lưới 24 app, cuộn được | khớp `apps-...`; cuộn tới cuối không cắt chữ |
| 5 | Trung tâm điều khiển | chạm “Điều khiển” hoặc vuốt xuống trên chrome | Wi-Fi hiện đúng trạng thái thật; toggle không backend ghi “Bật/Tắt”; thanh sáng gọi backlight | kéo thanh sáng đổi độ sáng thật; Wi-Fi không hiện “Đang bật” khi `wlan0` vắng |
| 6 | Cài đặt | chạm “Cài đặt” | hàng có chevron mở được; hàng “Chưa có dịch vụ” không phản hồi | chỉ các hàng có backend mới điều hướng |
| 7 | Lưu trữ | Cài đặt → “Lưu trữ” | % + “Đã dùng / tổng” từ `statvfs("/")`, RAM từ `/proc/meminfo` | khớp `storage-...`; thanh đổi màu khi ≥ 90% |
| 8 | Giới thiệu | Cài đặt → cuộn → “Giới thiệu VQEAF OS” | QEAFIVELS OS 0.3.0, nền tảng violet · PID 1, độ phân giải hiện tại, uptime tăng, pin/Wi-Fi thật | khớp `about-...`; uptime tăng sau ~1 phút; không có pin khi không có provider |
| 9 | Vuốt ngang | vuốt trái/phải trên trang | chuyển vòng giữa 4 trang nav; từ trang chi tiết quay lại Cài đặt | không bỏ qua trang, không kẹt |
| 10 | Xoay ngang | `send_rescue_cmd.py --cmd "rotate 90"` | bố cục 1560×720, lock không chồng chữ, nav vẫn dùng được | khớp `*-1560x720-*` (so với `--rotate` phù hợp); `rotate auto` trả lại dọc |
| 11 | Trạng thái thiếu phần cứng | xem Wi-Fi/Bluetooth/pin | “Không có thiết bị” / “Không có pin”; không có số giả | không xuất hiện phần trăm pin, cường độ sóng hay nhiệt độ bịa |
| 12 | Kênh cứu hộ vẫn sống khi UI lỗi | không cần thao tác | LUN và `QEAF.CMD` trả lời sau khi UI fault | `status` vẫn trả lời; UI fault không làm mất LUN |
| 13 | Dock kéo thả | giữ lâu icon trong dock hoặc trong lưới ứng dụng rồi kéo | icon nhấc lên bán trong suốt; thả vào slot để đổi chỗ/đưa app vào dock; thả ra ngoài để bỏ khỏi dock | dock đổi đúng, slot trống hiện dấu +, không có app trùng, app đang mở có viền accent |
| 14 | Wi-Fi | Cài đặt → “Wi-Fi” | giao diện wlan thật, operstate/MAC/link quality, module wlan+cfg80211, firmware WCN3990 | profile `unknown`: hiện “Không có giao diện mạng”, “Firmware: Thiếu”, không có SSID/số liệu bịa; nếu có firmware/netdev thì hiện đúng |
| 15 | Trình điều khiển & ổ đĩa | Cài đặt → “Trình điều khiển” | DRM card0, số event cảm ứng, UDC, đèn nền, pin, Wi-Fi; dung lượng root và QEAFRESCUE | khớp `drivers-...-unknown`; QEAFRESCUE đúng 32 MiB; p77 là phân vùng ghi duy nhất |
| 16 | Quét Wi-Fi + modal mật khẩu | Cài đặt → Wi-Fi → mạng bảo mật | danh sách SSID/mức tín hiệu/khoá; chạm mạng bảo mật mở modal có bàn phím | máy hiện “Không có giao diện mạng” khi thiếu firmware; nếu có netdev, danh sách là kết quả quét thật; kết nối bảo mật báo thiếu supplicant, không báo thành công giả |
| 17 | Tệp & đồng bộ | Cài đặt → cuộn → “Tệp & đồng bộ” | liệt kê tệp gốc QEAFRESCUE kèm dung lượng; nút Đồng bộ đọc lại | tệp PC thả vào hiện sau khi Đồng bộ; tệp DEVINFO/RESPONSE/SCREEN.RAW hiện đúng; không có bản sao thứ hai |
| 18 | Chủ đề màn hình khóa | Cài đặt → cuộn → “Màn hình khóa” | preview, 4 font, độ đậm, 7 màu, nút Xong | đổi font/màu/độ đậm áp dụng lên màn hình khóa thật; Xong quay về lock; lựa chọn chỉ trong phiên |
| 19 | File manager + QEAFRESCUE | Ứng dụng → Tệp | tab Hệ thống duyệt rootfs, tab QEAFRESCUE liệt kê volume FAT16 | vào/ra thư mục bằng `..`/back; thả tệp từ PC vào ổ, chạm Đồng bộ thì tệp xuất hiện; không sửa/xoá tệp hệ thống |
| 20 | Cài & mở QEAPP | copy `hello.qeapp` vào QEAFRESCUE → Tệp → QEAFRESCUE → Cài | modal xác nhận cài; Ứng dụng → Đã cài → Hello VQEAF mở trang có nội dung `print` | chữ ký đúng mới cài; bản bị sửa báo “Chữ ký không hợp lệ”; ứng dụng chỉ sống trong RAM |

## 9. Sự cố và quay lui

| Hiện tượng | Xử lý |
|---|---|
| Không có `ui: LVGL phone shell active` | UI không dựng: chụp `kmsg` qua kênh cứu hộ, đối chiếu log DRM; coi là FAIL, không tuyên bố giao diện chạy |
| Màn hình vẫn ở splash | kiểm tra `drm: verified ...`; nếu DRM fail, UI bị tắt có chủ ý và LUN vẫn sống |
| `snap: unavailable` | ảnh không có snapshot hook — dừng, kiểm tra lại đúng bản build |
| `tap:`/`swipe: unavailable` hoặc `refused` | ảnh chưa có hook, điểm ngoài canvas, UI chưa dựng hoặc đang có gesture chờ: build lại từ cây này, kiểm tra `ui: LVGL phone shell active`, dùng toạ độ ở mục 7b |
| Điện thoại treo, PC thấy `Generic Mass-Storage / No Media` | giữ **Volume Down + Power ~8 s**, cắm lại USB, chờ `fastboot devices`, rồi `restore_p77.ps1 -Image out/boot-qeafdropos-violet.img -Execute -ConfirmPartitionWrite` từ cây QeafdropOs (cần đồng ý) |
| Gate fail sau khi sửa | không nạp; sửa nguyên nhân, chạy lại từ mục 2 |

Tuyệt đối không nạp ảnh chưa qua hai gate, không ghi `p76/boot`, không dùng
`fastboot flash boot`, không nạp ảnh x86_64.

## 10. Giới hạn đã biết của quy trình

- **Tap được giữ 3 poll** (`violet_tap.h`): contact tổng hợp không còn chỉ sống
  một poll, nên hàng Settings/card không bị nuốt CLICK. Các container
  `vqeaf_obj`/`vqeaf_tile` cũng đã bỏ cờ CLICKABLE mặc định, nên chạm vào phần
  chữ hoặc icon đều tới handler của hàng/dock/card. Long-press của dock và app
  là 900 ms để tap 3 poll không kích hoạt kéo-thả. Nếu vẫn mất tap, kiểm tra
  `page=` trong `status` trước khi kết luận ảnh cũ.
- **`tap`/`swipe` không phải cảm ứng**: cả hai đã nối vào `vio_touch`
  (`vio_touch_inject`, `vio_touch_swipe`), nên chuyển trang, cuộn và chụp tự
  động được. Chúng chứng minh đường hit-test và đường cuộn của shell, **không**
  chứng minh driver cảm ứng thật hoạt động dưới ngón tay. Swipe vẽ một đường
  thẳng đều, không có quán tính như ngón thật; mỗi loại nhận một yêu cầu mỗi lần
  và bị từ chối khi UI chưa dựng (kiểm tra `ui: LVGL phone shell active`).
- **Snapshot không phải pixel-exact**: `snap` là 240×520 RGB565 lấy mẫu scanout
  1080×2340; render là canvas 720×1560 trước khi panel_scale. Sai khác lấy mẫu,
  RGB565 và khử răng cưa là bình thường — vì thế dùng ngưỡng, và luôn xem ảnh
  side-by-side.
- **Ảnh chụp có thể bị rách khung**: chụp lại (`snap` lần hai) nếu điểm khớp thấp
  mà mắt thấy bố cục đúng.
- **Giới thiệu nằm dưới màn hình đầu**: script tự swipe; làm tay thì phải cuộn
  ở trang Cài đặt trước khi chạm.
- **Pin/Wi-Fi**: chưa có backend trên violet; profile đúng là `unknown`. Nếu
  sysfs bắt đầu có dữ liệu thật thì chụp lại và dùng `--profile providers`.
- **`/` là initramfs trong RAM**: số liệu “Bộ nhớ trong” phản ánh root RAM, không
  phải `userdata`; không được diễn giải thành dung lượng eMMC.
- Chưa có app runtime, notification provider, recent apps; các mục đó không nằm
  trong checklist này.

## 11. Biên bản tối thiểu

Ghi lại: ngày giờ, `sha256` của `build/violet/vqeaf-init` và
`dist/boot-vqeaf-violet.img`, kích thước ảnh, kết quả 3 gate, các dòng
`HOMOLOG.TXT` ở mục 6, bảng checklist mục 8 với điểm khớp từng trang, ảnh
`compare-*.png`, và mọi mục FAIL kèm log liên quan. Không tuyên bố “giao diện
chạy trên máy” nếu chưa có ảnh `SCREEN.RAW` của ít nhất Lock, Home và một trang
chi tiết khớp gate.
