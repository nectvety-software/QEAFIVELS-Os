# Violet Wi-Fi: khoanh vùng modem/persist (2026-11-26)

Thiết bị: Redmi Note 7 Pro (`violet`, SM6150), serial fastboot `ca1af25d`.
Ảnh nạp: `dist/boot-vqeaf-violet.img`, 51.445.760 B,
SHA-256 `1B72829FD7F0725CD1B0D9D647FC27FADE604A7F5508E7307DD65B94E24F5B55`.
Chỉ ghi `p77/recovery`; không ghi `p76/boot`, `userdata`, `misc`, `firmware`, `persist`.

## 1. Nạp lại và xác minh

```
fastboot flash recovery dist\boot-vqeaf-violet.img
  Sending 'recovery' (50240 KB)  OKAY
  Writing 'recovery'             OKAY
```

Gate trước khi nạp: `validate_bootimg.py` **PASS**, `audit_rescue_channel.py`
**PASS** (flash_verb 5, rescue_shell 19, gadget_udc 5, init_panel 1).

Sau reboot, `HOMOLOG.TXT` (raw sector, `F:`) xác nhận đúng QEAF:

```
boot: QEAFIVELS OS 0.3.0 init on violet (PID 1)
lun: QEAFRESCUE volume ready (522240 sectors, 267386880 bytes)
usb: mass storage bound to a600000.dwc3 (/run/qeaf-lun.img)
drm: verified - crtc 107 scans our fb 188 at 1080x2340
ui: splash 720x1560 (portrait)
ui: LVGL phone shell active at 720x1560
boot: phone UI ready
```

## 2. Vì sao `wifi up` làm máy reset

Đo read-only trên máy (`wifi modem`):

```
subsys1 name=modem  restart_level=SYSTEM  state=OFFLINING
```

`restart_level=SYSTEM` nghĩa là **mọi crash của modem được xử lý ở mức hệ
thống**, tức reset cả SoC chứ không chỉ restart subsystem. Đây là cơ chế khớp
với quan sát: modem ONLINE → WLAN PD lỗi → modem crash → SoC reset → bootloader
vào fastboot (boot count 42 → 44 trong lần thử trước).

Không có `/sys/kernel/boot_modem`; `/sys/class/remoteproc` absent;
`/sys/kernel/boot_adsp` và `boot_cdsp` có, `boot_slpi` absent.
`/dev/wlan` tồn tại nhưng `wlan initstate` và `icnss initstate` đều absent.

## 3. Trạng thái Wi-Fi hiện tại (read-only)

| Hạng mục | Kết quả |
| --- | --- |
| `wlan0` | **absent** (`/sys/class/net` chỉ có lo, dummy0, bond0, sit0…) |
| WLFW QMI `0x45` | **không có server**; `qrtr-ns` có lookup `0x45/0x0` nhưng không ai trả lời |
| `wlfwd` | `not started` |
| `cnss-daemon` | `not started` (chỉ chạy sau `wifi vendor`) |
| `modem` | `state=OFFLINING`, chưa start (opt-in) |
| WLAN PD | `msm/modem/wlan_pd` từng trả `result=1 error=45`; restart `error 9` |

## 4. Dữ liệu persist (p74) — thiếu toàn bộ phần WLAN

`wifi ls /mnt/vendor/persist`:

```
display bluetooth vpp misc time secnvm tp_test camera hvdcp_opti sensors
fdsd data alarm lost+found qti_fp hlos_rfs rfs coresight drm stability
misys audio subsys iar_db
```

- **Không có** thư mục `wlan` (`wifi ls /mnt/vendor/persist/wlan` → absent/empty).
- **Không có** `wlan_mac.bin` (`wifi find wlan_mac.bin` → not found).

Vendor p86 có entry nhưng là **symlink gãy**:

```
/vendor/firmware/wlan/qca_cld/wlan_mac.bin -> /mnt/vendor/persist/wlan_mac.bin
wifi cat .../wlan_mac.bin: unreadable (errno 2)
```

## 5. Firmware modem (p82) và BDF

- p82 mount được ở `/vendor/firmware_mnt` (ext4, ro), 273 entry, có
  `modem.mdt` và `wlanmdsp.mbn`.
- `/vendor/firmware_mnt/image/wlan`: **absent/empty** → p82 không có BDF.
- `/vendor/firmware/wlan/qca_cld`: chỉ có `WCNSS_qcom_cfg.ini` và symlink
  `wlan_mac.bin` gãy → vendor p86 không có BDF.
- `/lib/firmware` (stage từ ROM zip) có `bdwlan.bXX`, `regdb.bin`,
  `wlanmdsp.mbn`, `modemuw.jsn` — nhưng đây là **bản generic**, không phải
  calibration của riêng máy.

`modemuw.jsn` (descriptor SERVREG) đã có và đúng:

```
msm/modem/wlan_pd  instance=180
services = kernel/elf_loader, tms/servreg, wlan/fw
```

Nên đây **không phải** lỗi thiếu descriptor dịch vụ.

## 6. Kết luận khoanh vùng

1. Blocker không nằm ở driver QEAF, không nằm ở QRTR/pd-mapper/tqftpserv/rmtfs,
   và không nằm ở descriptor SERVREG (`modemuw.jsn` đã đúng).
2. Blocker nằm ở **modem/WLAN PD + dữ liệu provisioning per-unit**:
   - persist p74 thiếu `wlan_mac.bin`, thư mục `wlan`, BDF và calibration;
   - chỉ có BDF generic trong `/lib/firmware`;
   - `wlan_pd` fail (`error=45`), modem có `restart_level=SYSTEM` nên kéo cả SoC
     reset.
3. Theo tài liệu `wlfwd`, BDF generic làm firmware crash ngay sau `CAL_REPORT`;
   dữ liệu đúng phải là bản của chính máy, lấy từ persist.

## 7. Hướng cần dữ liệu, không phải code

Để Wi-Fi chạy thật cần một trong hai:

- **Khôi phục persist gốc của máy** (backup TWRP/EDL của `persist`, hoặc bản
  factory provisioning): `wlan_mac.bin` + `bdwlan.bXX` + calibration. Đây là
  dữ liệu duy nhất của từng máy, không thể sinh lại từ code.
- **Provisioning bằng công cụ vendor** (không có trong repo, và cần quyền ghi
  `persist`).

Không thực hiện trong lần này:

- Không ghi `persist`/`modem`/`firmware`/`misc`/`userdata`.
- Không dùng MAC giả hay BDF generic để "cho có wlan0" (sai nguyên tắc trung
  thực của repo; firmware sẽ crash).
- Không mock QMI `0x45`.

## 8. Lệnh đã dùng (đều read-only với phân vùng)

```
wifi state
wifi modem
wifi ls /mnt/vendor/persist
wifi ls /mnt/vendor/persist/wlan
wifi ls /vendor/firmware/wlan
wifi ls /vendor/firmware/wlan/qca_cld
wifi ls /vendor/firmware_mnt/image
wifi find wlan_mac.bin
wifi find bdwlan.bin
wifi find wlanmdsp.mbn
wifi find modemuw.jsn
wifi cat /vendor/firmware/wlan/qca_cld/wlan_mac.bin
wifi link /vendor/firmware/wlan/qca_cld/wlan_mac.bin
diag
```

## Cập nhật phạm vi 2026-11-27

Prototype ICNSS mới được phát triển/test ở nhánh freestanding AArch64 + host, không làm thay đổi kết luận phần cứng của báo cáo ngày 2026-11-26. Không có bằng chứng mới cho thấy WLFW `0x45` đã publish, `wlan0` đã tồn tại hoặc Wi-Fi scan/association/DHCP hoạt động. Chưa bật WLAN-PD và chưa làm RAM boot trên Violet ở phiên này.

Chi tiết parser/IRQ/WLFW/UI, test counts và việc phân biệt prototype với production xem `docs/wifi/ICNSS_NATIVE_BACKEND_PHASE_2026-11-27.md` và `docs/wifi/ICNSS_TO_QEAF_PORT_MAP.md`.
