# Biên bản nạp VQEAF-OS vào Violet bằng fastboot — 2026-11-23

Thiết bị: Redmi Note 7 Pro (`violet`), serial `ca1af25d`.
Phạm vi ghi: **chỉ `p77`/recovery**. Không đụng `p76`/boot, `userdata`, `misc`
hay bất kỳ firmware partition nào. Đây là hành động có **đồng ý rõ ràng của
người dùng** (xác nhận chọn "Nạp img có sẵn" trước khi ghi), theo
`.agents/AGENTS.md` mục 8 và `.agents/skills/violet-port/SKILL.md`.

Biên bản này ghi lại một lượt nạp cụ thể; nó không thay thế
`docs/UI_DEVICE_REPORT_2026-11-11.md` hay các mục Wi-Fi trong `docs/STATUS.md`.

## 1. Ảnh và gate trước khi nạp

| Mục | Giá trị |
|---|---|
| Ảnh | `dist/boot-vqeaf-violet.img` |
| Kích thước | 51.318.784 byte |
| SHA-256 | `385A52924B86B328E4C91E9575803F4705A679071B58BE83B744B182D66FE4F0` |
| Thời điểm tạo ảnh | 2026-11-23 20:54 (local) |
| `validate_bootimg.py` | **PASS** — page 4096, kernel 17.505.611 B @0x00008000, ramdisk 33.807.111 B @0x01000000 |
| `audit_rescue_channel.py` | **PASS** — rescue_shell 19, help_text 3, response 4, flash_verb 4, sync_protocol 7, gadget_configfs 1, gadget_vendor 1, gadget_function 2, gadget_udc 5, init_panel 1, init_gadget 6 |

Gate được chạy lại ngay trước khi flash, dù ảnh đã được tạo bởi
`package_violet.py` (script này vốn đã chạy hai gate khi đóng gói). Không flash
ảnh nào chưa qua gate.

## 2. Trạng thái thiết bị và sự cố kết nối

- Ban đầu `fastboot devices -l` thấy `ca1af25d   fastboot`; không có ổ
  `QEAFRESCUE` nào mount (chỉ `C:`, `D:`, `E:` — `E:` là removable 0 byte).
  Vì vậy chọn đường fastboot thay vì đường rescue `F:`.
- Lệnh `fastboot getvar all` **(chỉ đọc)** bị treo hơn 120 s. Tiến trình
  fastboot treo đã bị kết thúc; sau đó thiết bị tạm thời không còn enumerate
  trong `fastboot devices` lẫn `adb devices`.
- Người dùng cắm lại và đưa máy về fastboot; `fastboot devices -l` lại thấy
  `ca1af25d   fastboot`. Không có partition nào bị ghi trong khoảng thời gian
  mất kết nối này.

## 3. Lệnh nạp và kết quả thật

```
fastboot flash recovery dist\boot-vqeaf-violet.img
  Sending 'recovery' (50116 KB)   OKAY [1.139s]
  Writing 'recovery'              OKAY [0.278s]
  Finished. Total time: 1.456s

fastboot reboot recovery          OKAY
```

PowerShell trả exit code 1 vì ghi stderr của fastboot thành `NativeCommandError`;
bản thân fastboot báo `OKAY` ở cả hai bước gửi và ghi.

## 4. Bằng chứng boot sau reboot

Ổ `QEAFRESCUE` mount lại tại `F:` (522.240 sector / 267.386.880 byte). Mọi đọc
dưới đây đều qua **raw sector** (`device_check.read_volume`), không qua mounted
view (view này từng bị stale).

Từ `HOMOLOG.TXT`:

```
boot: QEAFIVELS OS 0.3.0 init on violet (PID 1)
lun: QEAFRESCUE volume ready (522240 sectors, 267386880 bytes)
usb: write /config/usb_gadget/vqeaf/UDC failed: No such device
usb: mass storage bound to a600000.dwc3 (/run/qeaf-lun.img)
drm: verified - crtc 107 scans our fb 188 at 1080x2340
ui: splash 720x1560 (portrait)
boot: splash painted; starting phone UI
ui: LVGL touch input bound to /dev/input/event1
ui: LVGL phone shell active at 720x1560
boot: phone UI ready
```

Dòng `UDC failed: No such device` xuất hiện một lần trước khi bind, đúng hành vi
đã ghi trong skill (UDC chưa đăng ký, vòng retry sẽ bind).

Từ `QEAFBOOT.LOG`:

```
[    0.000] N qeaf: QEAFIVELS OS 0.3.0 starting (pid 1)
[    0.010] N qeaf: rescue channel bound to the RAM LUN
[    -    ] N qeaf: boot stage dri-wait
[    -    ] N qeaf: boot stage loop
```

Từ `LASTBOOT.TXT`: `boots=2 (this boot)`, `no fault record from the previous boot`.

Ảnh chụp thật qua verb `snap` (chỉ ghi `QEAF.CMD` trên LUN RAM):

```
python scripts/device_check.py --volume F: --steps "snap flashed"
snap  flashed  frame=1 a5594def90fc stage=loop
      build\screens\device\check\flashed.png
PASS
```

Ảnh cho thấy LVGL phone shell đang chạy (lưới ứng dụng, widget pin/settings,
thanh tìm kiếm, dock điều hướng) ở canvas 720x1560.

### Lưu ý về bằng chứng ghi

Lượt này dùng fastboot nên **không có** dòng
`flash: written and verified byte for byte` trong `RESPONSE.TXT` và cũng không
có dòng `lun: verified - recovery and BOOTIMG.BIN agree byte for byte` trong
`QEAFBOOT.LOG` — hai dòng đó chỉ xuất hiện khi flash qua verb rescue
(`device_check.py --flash` / `prepare_rescue_flash.py --commit`). Bằng chứng ghi
của lượt này là fastboot `OKAY` ở cả send lẫn write, cộng với việc máy boot trực
tiếp từ nội dung p77 vừa ghi và đạt tới `stage=loop` / phone UI. Không được diễn
giải đây là bằng chứng byte-for-byte kiểu rescue.

## 5. Giới hạn vẫn còn

- PID 1 này chạy trên **stock kernel 4.14.215**, là transport phát triển, chưa
  phải QEAFIVELS Kernel độc lập (giai đoạn 3).
- Wi-Fi/GNSS tiếp tục **BLOCKED** ở lớp firmware/dịch vụ: WLFW QMI `0x45`
  không được publish, `wlan0` absent; GNSS không có device node. Không báo kết
  nối giả.
- Cài đặt (theme/font/PIN/nav mode) chỉ nằm trong RAM, mất khi reboot.
- Ảnh `dist/vqeaf-x86_64-*.img` là bản tham chiếu QEMU; tuyệt đối không nạp vào
  máy thật.

## 6. Thay đổi source

Không có file nguồn nào trong repo bị sửa bởi lượt nạp này. Artifact duy nhất
tạo ra nằm ngoài quản lý nguồn: `build/screens/device/check/flashed.png`.

## 7. Bước kiểm chứng nhỏ tiếp theo

- `python scripts/device_check.py --volume F: --scenario smoke` để kiểm tra
  Lock/Home/Settings/Files trên chính máy vừa nạp.
- Nếu cần bằng chứng ghi kiểu rescue, flash lại qua `F:` bằng
  `python scripts/device_check.py --volume F: --image dist/boot-vqeaf-violet.img --flash`
  (verb `flash recovery ... confirm` sẽ tự ghi p77 và sinh hai dòng proof).
