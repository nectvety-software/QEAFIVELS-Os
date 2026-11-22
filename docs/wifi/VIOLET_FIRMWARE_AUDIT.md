# Violet firmware audit (Giai đoạn 2)

Tool: `scripts/wifi/inspect_violet_firmware.py` (read-only).
Manifest: `firmware/violet/manifest.json` (SHA-256 từng member).

## Nguồn đã kiểm kê
- `build/violet-fw.zip` — 69.667.787 B, firmware-update package (19 members,
  158.605.828 B uncompressed). Chứa `NON-HLOS.bin`, `BTFM.bin`, `dspso.bin`,
  bootloader/TZ và各 bản vá an toàn. **WLAN image nằm bên trong NON-HLOS.bin**,
  không phải file rời trong ZIP.
- `build/violet-fw/image/` — kết quả trích `NON-HLOS.bin` từ bản MIUI
  `20.10.30` (workspace cũ), có `wlanmdsp.mbn` (3.694.736 B) và 28+ biến thể
  `BDWLAN.*` + `regdb.bin`. Đây là firmware thật đã dùng trong bản dựng hiện tại.
- `/vendor/firmware_mnt/image` trên máy: mount readonly từ `mmcblk0p82`,
  chứa `modem.*`, `wlanmdsp.mbn` và 273 entry theo report thiết bị.
- `/lib/firmware/`: `wlanmdsp.mbn`, `bdwlan.bin`, `regdb.bin` được stage bởi
  `scripts/package_violet.py`; report `wifi fw` trên máy xác nhận hash.

## Kết quả tool
`required_present` trên **ZIP** là false cho `wlanmdsp.mbn`, `bdwlan`, `regdb.bin`
vì đó là file bên trong `NON-HLOS.bin`, không phải member ZIP. Đây là kết quả đúng
và được ghi trong manifest, không phải lỗi. Cầntract (script hiện có
`scripts/extract_stock_firmware.sh`) rồi mới thấy các file WLAN.

Không phát hiện duplicate payload trong ZIP (`duplicates: 0`).

## Kiểu firmware
| Nhóm | Tên | Vai trò | Nguồn |
| --- | --- | --- | --- |
| WLAN DSP | `wlanmdsp.mbn` | nạp cho WCN3990 | `NON-HLOS.bin` (MIUI 20.10.30), sha256 ghi trong `radio-firmware-status.txt` trên máy |
| Board data | `BDWLAN.*` | per-SKU board data | cùng `NON-HLOS.bin`, 28 biến thể, mỗi 26.328 B |
| Regulatory | `regdb.bin` | regulatory DB | cùng `NON-HLOS.bin` |
| Bluetooth | `BTFM.bin` | BT firmware | member ZIP `firmware-update/BTFM.bin` |
| Modem | `NON-HLOS.bin`, `modem.mdt/mba.mbn` | modem image | member ZIP |
| DSP | `dspso.bin` | ADSP/CDSP | member ZIP |

## Vùng firmware theo khu vực (LineageOS Wiki, 2026-11-22)
India: `V12.5.1.0.QFHINXM`; China: `V12.5.4.0.QFHCNXM`.
Thiết bị hiện tại có `ro.boot.hwc=CN` (dmesg), nhưng **chưa xác nhận SKU/MIUI
version đầy đủ** — không tự chọn bản firmware, không flash, không trộn vùng.

## Còn thiếu / chưa được chứng minh
- Factory MAC (`wlan_mac.bin`) và calibration per-unit (`wlfw_cal_*`) trên p74:
  **không có**, đây là blocker đầu tiên. Không tạo giả.
- Không tự tải firmware India/China mới; giữ nguyên bản đã có trên máy.
- Không đính kèm binary có giấy phép không rõ vào repo; chỉ ghi nguồn + hash.

Mọi thao tác trên là đọc/trích xuất trên máy tính; **không ghi thiết bị**.
