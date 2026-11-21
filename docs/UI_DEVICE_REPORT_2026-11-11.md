# Biên bản kiểm thử VQEAF UI trên Redmi Note 7 Pro — 2026-11-11

Thiết bị: Redmi Note 7 Pro (`violet`), kênh cứu hộ `QEAFRESCUE` ở `F:`.
Quy trình: `docs/UI_DEVICE_TEST.md`. Ảnh so sánh nằm trong
`build/screens/phone-ui/`, capture thô trong `build/screens/device/`.

## 1. Ảnh đã nạp

| Mục | Giá trị |
|---|---|
| ELF `build/violet/vqeaf-init` | 6.414.896 byte, 235 object, sha256 `b08dfc85ac0d3f12d5d1c866c8085ebd74d8996f500ba186fb3e26e9846c041d` |
| Ảnh `dist/boot-vqeaf-violet.img` | 19.562.496 byte, sha256 `8587b55180124370557e3d581da796a136b4991e79201fc4f09d5bacc2a62c50` |
| Kích thước `p77` | 67.108.864 byte (ảnh chiếm 29%) |
| Gate build / render / package | PASS (build, `720x1560 ok (7 pages x 2 profiles)`, `1560x720 ok`, 28 PNG; `validate_bootimg` + `audit_rescue_channel` PASS) |

Nạp: `prepare_rescue_flash.py --image dist/boot-vqeaf-violet.img --volume F: --commit`,
chỉ `p77`. Bằng chứng:

- `RESPONSE.TXT`: `flash: written and verified byte for byte`
- `QEAFBOOT.LOG`: `lun: verified - recovery and BOOTIMG.BIN agree byte for byte over 19562496 bytes`

## 2. Boot sau khi nạp (HOMOLOG.TXT, đọc raw sector)

```
boot: QEAFIVELS OS 0.3.0 init on violet (PID 1)
lun: QEAFRESCUE volume ready (65536 sectors, 33554432 bytes)
usb: mass storage bound to a600000.dwc3 (/run/qeaf-lun.img)
boot: rescue worker owns QEAF.CMD
drm: verified - crtc 107 scans our fb 188 at 1080x2340
ui: splash 720x1560 (portrait)
ui: LVGL touch input bound to /dev/input/event1
ui: LVGL phone shell active at 720x1560
```

Dòng `usb: ... UDC failed: No such device` xuất hiện một lần trước khi bind, đúng
như tài liệu mô tả.

## 3. Checklist mục 8

Ngưỡng của `ui_snapshot_compare.py`: ≥ 90% pixel trong sai số 40/channel và mean ≤ 12.

| # | Mục | Kết quả | Bằng chứng |
|---|---|---|---|
| 1 | Lock | **PASS** 99,8% (mean 0,3) ở lần chụp ngay sau khi nạp | `compare-lock.png` |
| 2 | Mở khóa bằng vuốt | **PASS** 98,7% (mean 1,5) sau `swipe 360 1200 360 400 12` | `unlock-swipe.raw`, `compare-home.png` |
| 3 | Home | **PASS** 98,7% (mean 1,5) | `compare-home.png` |
| 4 | Ứng dụng | **PASS** 98,1% (mean 2,2) | `compare-apps.png` |
| 5 | Trung tâm điều khiển | **PASS** 98,3% (mean 1,9) cho bố cục; **chưa kiểm** thanh sáng đổi backlight thật | `compare-control.png` |
| 6 | Cài đặt | **PASS** 97,7% (mean 2,0) | `compare-settings.png` |
| 7 | Lưu trữ | **FAIL** 92,9% (mean 15,7) — xem mục 4 | `compare-storage-final.png` |
| 8 | Giới thiệu | **PASS** 98,3% (mean 2,2) sau `swipe (360,828)→(360,100)` + `tap (360,1360)` | `compare-about.png` |
| 9 | Vuốt ngang | **PASS** 97,7%: `swipe 200 700 660 700 12` từ trang chi tiết quay về Cài đặt | `swipe-back.raw` |
| 10 | Xoay ngang | **PASS** 99,5% (mean 0,6) cho About ở 1560×720, `rotate 90` rồi `rotate 0` | `landscape-about.raw`, `compare-about-landscape.png` |
| 11 | Trạng thái thiếu phần cứng | **PASS**: ảnh máy ghi “Không có thiết bị” cho Wi-Fi/Bluetooth/di động, không có % pin hay cường độ sóng bịa | `compare-settings.png`, `compare-control.png` |
| 12 | Kênh cứu hộ sống khi UI chạy | **PASS**: `status` trả lời `stage=loop; ui=1; drm=1; uptime=...s`, luôn kèm nonce | `RESPONSE.TXT` |

Uptime tăng: `955 s` → `1242 s` trong hai lần đọc `status` cách nhau ít phút
(trước lần reboot cuối); sau reboot đọc được `178 s` rồi `420 s`.

## 4. Mục 7 (Lưu trữ) — FAIL và những gì đã biết

Chuỗi `tap (613,1504)` → `tap (360,1047)`:

- Cả hai tap đều được nhận (`tap: queued for the UI thread`), nhưng ảnh chụp lại
  là **Settings**, có lần là **Home**, chưa lần nào là Lưu trữ.
- Toạ độ không sai: đo trên render `settings-720x1560-unknown.ppm`, hàng đầu bắt
  đầu y=256, pitch 146, hàng “Lưu trữ” (index 5) là 986..1107, tâm 1047. Tap
  y=900 (hàng “Hiển thị”) lại mở đúng Trung tâm điều khiển, nên hàng có handler.
- Giả thuyết: tap tổng hợp chỉ giữ trạng thái press **một poll** của UI thread.
  Khi LVGL còn đang vẽ lại trang vừa dựng, vòng lặp có thể bỏ qua đúng poll press
  và chỉ thấy release, nên CLICK không bao giờ phát. Các trang mở được đều có
  ít nhất một thao tác khác xen giữa lần dựng trang và cú tap quyết định.

Đề xuất sửa (chưa làm): giữ press 2–3 poll trước khi release trong
`present_injected()`, và cho `vio_tap_present()` trả press lặp lại thay vì đúng
một lần; test host cần cập nhật theo.

## 5. Đặc tính máy thật phát hiện trong lượt này

1. **Ảnh chụp trễ**: `snap` ngay sau tap 0,8 s trả về trang trước. Script nay lặp
   `snap` tới khi hai khung liên tiếp trùng nhau (`--tries`, mặc định 5).
2. **Tự xoay làm hỏng toạ độ**: sau `rotate auto` máy ở 1560×720, tap theo toạ độ
   dọc bị `refused (outside the canvas or UI down)` và ảnh vẫn là trang cũ.
   Script từ chối chạy khi `status` báo landscape.
3. **Tap ngay sau khi đổi trang có thể bị mất** (mục 4).
4. **Sau reboot shell lên thẳng Home**, không phải Lock: lần chụp “lock” thứ hai
   cho ra Home (76,2% so với render lock). PASS 99,8% ở mục 1 là lần chụp ngay
   sau khi nạp ảnh, trước khi có tap nào.

## 6. Thay đổi công cụ trong lượt này

- `scripts/ui_capture_pages.py`: mặc định chờ 3 s; lặp snap tới khung ổn định;
  từ chối chạy khi landscape; dừng ngay khi tap/swipe bị `refused`/`unavailable`;
  thêm `--tries`.
- `scripts/ui_snapshot_compare.py`: thêm `--landscape` để so capture sau
  `rotate 90` với render 1560×720.
- `docs/UI_DEVICE_TEST.md` mục 7/7b/10: ghi ba đặc tính trên và cờ mới.

## 7. Kết luận

Ảnh nạp đúng và xác minh byte-for-byte; máy boot PID 1, DRM quét framebuffer của
mình, shell LVGL chạy và nhận cảm ứng thật (`/dev/input/event1`). Sáu trang
(Lock, Home, Ứng dụng, Điều khiển, Cài đặt, Giới thiệu) khớp render 97,7–99,8%,
xoay ngang khớp 99,5%, và các cử chỉ `swipe` (mở khóa, cuộn tới Giới thiệu, quay
lại từ trang chi tiết) hoạt động. **Chưa đạt**: trang Lưu trữ (tap hàng bị mất,
nghi do press chỉ kéo dài một poll) và kiểm tra thanh sáng đổi độ sáng thật.
Không có mục nào trong biên bản này dựa trên suy đoán: mục nào chưa kiểm đều ghi
“chưa kiểm”.
