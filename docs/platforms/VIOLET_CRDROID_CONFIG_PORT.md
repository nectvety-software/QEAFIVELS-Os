# Port từ crDroid `android_device_xiaomi_violet` (2026-11-22)

Nguồn: https://github.com/crdroidandroid/android_device_xiaomi_violet
Commit đã audit: `faab5ba`. Clone read-only vào temp để kiểm kê.

## Kết luận audit: repo này KHÔNG có driver C
- `wifi/` chỉ có 3 file config: `WCNSS_qcom_cfg.ini`,
  `wpa_supplicant_overlay.conf`, `p2p_supplicant_overlay.conf`.
- `BoardConfig.mk` đặt `BOARD_WLAN_DEVICE := qcwcn` — driver **qcacld-3.0**
  và `icnss` nằm trong kernel/vendor, **không có trong device tree này**.
- `Android.mk` tạo symlink:
  - `/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini` → `/vendor/etc/wifi/...`
  - `/vendor/firmware/wlan/qca_cld/wlan_mac.bin` → `/mnt/vendor/persist/...`
  Đây là bằng chứng xác nhận hướng đi của `wifi_bringup.c` hiện tại.
- Các file `.cpp` trong `gps/`, `fingerprint/`, `light/`… là **Android HIDL
  services** — bị cấm đưa vào QEAFIVELS OS. Không port.

## Cái được port (data, không phải code Android)
| File QEAF | Nguồn crDroid | Ghi chú |
| --- | --- | --- |
| `firmware/violet/config/WCNSS_qcom_cfg.ini` | `wifi/WCNSS_qcom_cfg.ini` | Đã **vô hiệu hoá** 2 dòng MAC placeholder của vendor |
| `firmware/violet/config/wpa_supplicant_overlay.conf` | `wifi/wpa_supplicant_overlay.conf` | Giữ nguyên, sha ghi trong manifest |
| `firmware/violet/config/p2p_supplicant_overlay.conf` | `wifi/p2p_supplicant_overlay.conf` | Giữ nguyên |
| `firmware/violet/config/manifest.json` | do script sinh | URL + commit + SHA-256 nguồn/đích |

Tool: `scripts/wifi/port_crdroid_config.py` (`--source <checkout> --out <dir>`).

### MAC placeholder — không được dùng
`WCNSS_qcom_cfg.ini` của vendor có:
```
Intf0MacAddress=000AF58989FF
Intf1MacAddress=000AF58989FE
```
và chính comment gốc nói *"will be used until NV items are in place"*.
Đây **không phải MAC của máy**. Script chuyển thành
`# Intf0MacAddress=<removed>` kèm giải thích rằng MAC thật phải đến từ
`/mnt/vendor/persist/wlan_mac.bin`. Không tạo MAC giả, không thay bằng MAC
cố định khác.

## Tích hợp build
`package_violet.py` giờ lấy config từ `firmware/violet/config/` (tracked),
`build/config-wifi/` chỉ còn là override local. File được stage tới:
- `/lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini`
- `/vendor/etc/wifi/WCNSS_qcom_cfg.ini`
- `/vendor/etc/wifi/{wpa,p2p}_supplicant_overlay.conf`

Gate bổ sung: từ chối đóng gói nếu config không được stage hoặc nếu chuỗi MAC
placeholder xuất hiện. Các gate cũ (`validate_bootimg`, `audit_rescue_channel`,
`net:`) **không bị suy yếu**.

## Kiểm chứng
- `python -m unittest tests.wifi.test_crdroid_config_port` → **4 tests OK**.
- `python scripts/build_violet.py` → OK (11.400.872 B, 280 objects).
  Cần thiết: thêm `qvf_encode.c` (file có sẵn trong workspace nhưng thiếu
  trong SOURCES) — đây là công việc recorder trước đó, không phải driver.
- `python scripts/package_violet.py` → OK, ảnh sha
  `c246cbae4a6a08dfea78682f900889359881a26f4bc04283b29149884dca89ee`.
- Giải nén ramdisk: `gWlanMccToSccSwitchMode=True`, `WCNSS_qcom_cfg.ini=True`,
  `000AF58989FF=False`.

## Giới hạn
Config chỉ là dữ liệu. Nó **không giải quyết** blocker WLFW `0x45`, persist
thiếu `wlan_mac.bin`/calibration, hay modem reset. Không có driver C nào được
port vì repo nguồn không có. Không flash, không ghi phân vùng.
