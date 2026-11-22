# Violet Wi-Fi: kiểm tra container firmware trước khi giao cho icnss

Ngày: 2026-11-22. Phạm vi: driver/firmware phía QEAF (không flash, không ghi
partition, không đụng modem/EFS/persist).

## Vấn đề

`wifi_firmware.c` trước đây chỉ đọc blob qua callback và băm FNV-1a: một file
bị cắt cụt, sai class ELF hay không phải ảnh Hexagon vẫn được coi là "đã nạp".
Trong khi đó `wlanmdsp.mbn` là **mã thực thi của WCN3990** — giao sai file cho
`icnss`/`qcacld` chỉ tạo ra lỗi khó đọc ở tầng sau. Firmware thật đã được stage
sẵn trong ramdisk (`/lib/firmware`), nên chỗ thiếu là bước kiểm tra cấu trúc.

## Đã thêm

### 1. Kiểm tra container trong driver port được

`kernel/include/vqeaf/wifi_firmware.h` + `kernel/drivers/wifi/wcn3990/wifi_firmware.c`:

- `vqeaf_wifi_fw_probe()` đọc **tối đa 4 KiB header** (không cần nạp cả 3,7 MB)
  và đối chiếu với kích thước file thật qua callback `size`.
- ELF32/ELF64, chỉ little-endian: kiểm `e_phoff`/`e_phentsize`/`e_phnum`, bảng
  program header phải nằm trong file; mỗi `PT_LOAD` phải có `p_offset +
  p_filesz <= file_size` (chống overflow) và tổng payload > 0; `e_entry` phải
  nằm trong một `PT_LOAD` (`p_vaddr .. p_vaddr+p_memsz`).
- Board data (BDF): u16 version 1/2, file ≥ 4 KiB và bội số 4 byte.
- Regulatory DB: magic `0x00040ED3`, file ≥ 4 KiB.
- `vqeaf_wifi_fw_check_set()` kiểm cả bộ ba `wlanmdsp.mbn` + `bdwlan.*` +
  `regdb.bin`; ngoài container, **DSP phải là ELF32 machine 164 (EM_QDSP6 /
  Hexagon)**. `set->ok = 1` chỉ khi cả ba cùng đạt; lỗi trả về là lỗi đầu tiên
  gặp (`-ENOENT` thiếu file, `-ENODATA` rỗng, `-EINVAL` hỏng/cụt,
  `-ENOTSUP` sai container/machine).

### 2. Lệnh `wifi fw` trên máy in kết quả thật

`platform/violet/wifi_fw_check.{c,h}` dùng `open()/stat()/read()` trên đúng các
file trong ảnh, rồi `rescue.c` gọi nó sau phần liệt kê thư mục firmware:

```
fw check: dsp /lib/firmware/wlanmdsp.mbn: elf32 machine=164 entry=0xb0000000 segments=3 payload=3121976 size=3694736 OK
fw check: board /lib/firmware/bdwlan.bin: bdf size=26328 OK
fw check: regdb /lib/firmware/regdb.bin: regdb size=19348 OK
fw set: OK (wlanmdsp+bdf+regdb match the driver contract)
```

Khi hỏng, mỗi dòng ghi `FAILED rc=<errno> (<lý do>)` và dòng cuối là
`fw set: FAILED (rc=…; icnss must not be handed this set)`. Không có nhánh nào
in "OK" từ dữ liệu lưu sẵn hay từ việc build thành công.

## Bằng chứng

| Hạng mục | Kết quả | Bằng chứng |
| --- | --- | --- |
| Test host container | **PASS** | `scripts/wifi/run_hal_tests.py`: `test_wifi_firmware: ALL OK` |
| Ảnh Hexagon thật | **PASS** | assert trên file thật: 3.694.736 B, 3 `PT_LOAD`, payload 3.121.976 B, entry `0xb0000000`, machine 164 |
| BDF + regdb thật | **PASS** | `bdwlan.bin` 26.328 B → `bdf`; `regdb.bin` 19.348 B → `regdb` |
| Thiếu/hỏng firmware bị từ chối | **PASS (host)** | cắt header, `phnum=0`, sai `phentsize`, sai class, big-endian, `PT_LOAD` vượt file, entry ngoài segment, file rỗng/thiếu |
| Bộ ba không đủ | **PASS (host)** | thiếu board → `-ENOENT`, `ok=0`; DSP sai machine → `-ENOTSUP`, `ok=0` |
| Build ảnh violet | **PASS** | `dist/boot-vqeaf-violet.img` 32.534.528 B, sha256 `139075f2d5149d6181956c34a357b4fdc747665eb0530f250657ec5338b4e356` |
| `wifi fw` trên máy thật | **CHƯA CHẠY** | máy đang rút cáp lúc phát triển; cần flash ảnh trên rồi chạy `wifi fw` |
| `wlan0` / scan / data | **VẪN FAILED** | chưa có netdev; feature này **không** làm Wi-Fi lên mạng |

## Vì sao vẫn chưa có Wi-Fi

Kiểm tra container không thay được các blocker đã ghi trong
`VIOLET_WIFI_AUDIT.md`: persist (p74) thiếu `wlan_mac.bin`/calibration thật,
WLFW `0x45` chưa publish nên `icnss` không có "firmware ready", modem reset
~40 s sau khi ONLINE, và kernel tự viết chưa có network stack. Giá trị của
thay đổi này: khi một trong các blocker đó được gỡ, driver từ chối firmware
hỏng ngay tại chỗ thay vì đẩy lỗi mơ hồ xuống dưới, và `wifi fw` chứng minh
được bằng số liệu mà máy đang mang.

## Không làm

Không tạo/ký firmware, không bỏ qua kiểm tra khi file thiếu, không đọc/ghi
partition, không flash firmware mới, không tuyên bố `WLAN_READY`.
