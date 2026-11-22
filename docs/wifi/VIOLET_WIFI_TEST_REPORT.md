# Violet Wi-Fi test report — QRTR/QMI real IO + firmware boot (2026-11-22)

## Môi trường
- Thiết bị: Xiaomi Redmi Note 7 Pro (`violet`, Snapdragon 675 / WCN3990),
  serial `ca1af25d`.
- Ảnh RAM boot (p77) đã flash, **sha256**
  `c9e927045f4529b221c674d131bbe4d62b54a2f39521183b096638643369338c`,
  size `32.485.376 B`. Chỉ boot tạm; **không ghi phân vùng modem/EFS/persist/fsg/NV**.
- Lệnh chạy qua QEAFRESCUE `QEAF.CMD`, đọc `RESPONSE.TXT`.

## Đo discovery bằng IO thật (AF_QIPCRTR socket trong PID1)

Lệnh `wifi discover` — dịch vụ WLFW `0x45`, bản ghi nguyên văn:
```
qrtr-live: service=0x45 instance=1 version=1 local_node=0x1
qrtr-live: lookup_sent=1 timeout_ms=1500 elapsed_ms=1502 recv_calls=0 recv_bytes=0
qrtr-live: server=NOT_REGISTERED rc=-2 (ENOENT) BLOCKED
qrtr-live: note=only received NEW_SERVER can verify
```

**Thí nghiệm đối chứng** chứng minh đường IO thật hoạt động (không phải lúc nào
cũng trả lỗi). Lệnh `wifi discover 18` — dịch vụ `0x18/1` có thật trong bảng:
```
qrtr-live: service=0x18 instance=1 version=1 local_node=0x1
qrtr-live: lookup_sent=1 timeout_ms=1500 elapsed_ms=0 recv_calls=1 recv_bytes=20
qrtr-live: last_ctrl_cmd=4
qrtr-live: server=FOUND node=0x5 port=0x8 VERIFIED
```
`last_ctrl_cmd=4` = `QRTR_TYPE_NEW_SERVER`, đúng 20 byte control packet.
Như vậy kết quả `0x45` là có ý nghĩa: đây là **BLOCKED thật**, không phải bug client.

## Đối chiếu với name service có sẵn (`wifi svc`, cùng thiết bị)
```
qrtr-ns: nid=0x1 bound=1 lookups=24 servers=24 nodes=3 fwd=72 rebinds=0 short=0
qrtr-ns lookups: 0x45/0x0->0x4024 ... (có lookup 0x45)
qrtr-ns servers: 0x1001/0x0@0x1:0x0 0x34/0x1@0x1:0x0 ... 0x301/0x0@0x5:0x14
wifi: wlan0 present: no
```
Có lookup `0x45` nhưng **không có server `0x45`** trong 24 server đã thấy.

## Giai đoạn 5 — firmware boot state machine
Implementation: `kernel/drivers/wifi/wcn3990/firmware_boot.c` +
`kernel/include/vqeaf/firmware_boot.h`. Chuyển trạng thái chỉ khi callback
đọc được xác nhận thật (`fw_boot_status` phải trả `booted=1`); poll có deadline,
không dùng sleep cố định làm bằng chứng boot; crash → restart có giới hạn;
 thiếu service → `FAILED`, **không bao giờ** `WLAN_READY`.

Test host: `test_firmware_boot: ALL OK` — full path, fail ở từng bước 1..8,
boot timeout (`11` lần poll/`-ETIMEDOUT`), crash 1 lần + restart, crash liên tục
hết retry, QMI không thấy service → `-ENOENT`, abort giải phóng resource.

## Bảng acceptance (trung thực)
| Hạng mục | Kết quả | Bằng chứng |
| --- | --- | --- |
| QRTR transport (control codec 20B) | **PASS** | fixture + `wifi discover 18`: cmd=4, 20B thật |
| Service discovery `0x45` | **BLOCKED** | `recv_calls=0`, `NOT_REGISTERED`, `ENOENT`, 1502 ms |
| Service discovery điều khiển | **PASS** | `0x18` → FOUND node=0x5 port=0x8 |
| QMI client encode/decode, txn, retry | **PASS (host)** | `test_qrtr_qmi: ALL OK` |
| Firmware boot state machine | **PASS (host) / NOT TESTED (device)** | logic tested; không thể chạy tới WLAN_READY khi thiếu WLFW |
| `wlan0` / scan / DHCP / data | **FAILED** | `wlan0 present: no`; không có bằng chứng |

## Sự cố boot sau lần flash thứ hai
Sau khi đổi tên file bằng chứng từ `WIFIDISCOVER.TXT` (12 ký tự, vượt FAT 8.3)
thành `QRLIVE.TXT`, ảnh mới (sha `6fdeed288d8e00ff75bc044e705100cade74f98e2b92592d8c7ac8288b2d0315`)
đã build/package/gate PASS và flash PASS, nhưng thiết bị kẹt ở boot stage
`splash`, không nhận `QEAF.CMD` dù đã chờ ~160 s. Đây là hiện tượng boot flaky
đã gặp trước đó; cần **power-cycle tay (giữ nguồn 10–15 s)**. Vì vậy file
`QRLIVE.TXT` **chưa được xác minh trên máy** — không báo PASS. Mọi số liệu ở
trên đến từ lần flash đầu (đã boot bình thường).

## Không làm
Không flash firmware/modem/EFS/persist/fsg/NV, không EDL, không tự cập nhật
firmware vùng India/China, không tạo server/QMI response giả, không ghi
partition. Không tuyên bố Wi-Fi hoạt động.
