# Violet QRTR / QMI native client (Giai đoạn 4)

## Nguyên tắc
Không copy codec BSD của Linaro (`platform/violet/hosttools/src/qrtr/lib/qmi.c`)
và không sửa license của nó. Lớp dưới đây là implementation độc lập từ wire
format công khai, chạy được cả trong kernel freestanding lẫn host test.

**Không giả:** module này là client. Nó chỉ gửi `NEW_LOOKUP`, chỉ đưa server
vào bảng khi **nhận** được `NEW_SERVER` thật, và chỉ coi một response là thành
công khi bytes nhận được khớp transaction ID + message ID + độ dài. Không bao
giờ tự tạo response, tự đăng ký service, hay tự báo WLFW sẵn sàng.

## Files
| File | Vai trò |
| --- | --- |
| `kernel/include/vqeaf/qrtr_transport.h` | control packet, endpoint, IO callback, API |
| `kernel/drivers/wifi/wcn3990/qrtr_transport.c` | codec LE 20-byte, server table, discovery |
| `kernel/include/vqeaf/qmi_service.h` | header/TLV, client state, pending table, indication API |
| `kernel/drivers/wifi/wcn3990/qmi_service.c` | txn id, matching, timeout, retry, reset, indication receive |
| `kernel/include/vqeaf/wlfw_client.h` | hợp đồng client WLFW `0x45`, message/TLV và giới hạn |
| `kernel/drivers/wifi/wcn3990/wlfw_client.c` | register/MSA/CAP/BDF/CAL/MAC, chỉ nhận readiness thật |
| `kernel/include/vqeaf/firmware_boot.h` | gate boot có trạng thái `WLFW_READY` riêng |
| `kernel/drivers/wifi/wcn3990/firmware_boot.c` | không chuyển thẳng discovery sang `WLAN_READY` |
| `tests/wifi/gen_qrtr_qmi_fixtures.py` | tạo wire packet FIXTURE |
| `tests/wifi/test_qrtr_qmi.c` | test protocol, indication và mọi nhánh lỗi |
| `tests/wifi/test_wlfw_client.c` | mock transport kiểm tra từng request/segment/indication |
| `tests/wifi/test_firmware_boot.c` | regression chống ready giả |
| `tests/wifi/test_qrtr_qmi.py` | wrapper unittest + guard |
| `tests/wifi/test_wlfw_client.py` | wrapper unittest + source guard |

## Wire format đã xác nhận
- QRTR control: `cmd(u32 LE) + service + verinst + node + port` = **20 bytes**
  (`instance` là byte cao của `verinst`, version là byte thấp). `NEW_SERVER=4`,
  `DEL_SERVER=5`, `NEW_LOOKUP=10`, `BYE=3`. Đối chiếu với `qrtr_ns.c` trên máy.
- QMI: header 7 bytes `type | txn_id(u16 LE) | msg_id(u16 LE) | msg_len(u16 LE)`;
  TLV `type(u8) | len(u16 LE) | data`. Result TLV type 0 chứa `result, error`
  (mỗi u16 LE).

## Hành vi chống giả
- `vqeaf_qrtr_discover()`: gửi 1 lookup thật tới `{local_node, CTRL}` rồi đọc
  control packet tới deadline. Không có `NEW_SERVER` → `-ENOENT`. Không bịa
  node/port. RECV trả lỗi (`-ENODEV`/`-ENETRESET`) → propagate nguyên vẹn.
- `vqeaf_qmi_request()`:
  - transaction id tăng dần, bỏ qua 0, pending table 8 slot;
  - indication được đếm riêng và chuyển cho handler; không nhầm là response;
  - response sai `txn_id`/`msg_id`/`type` hoặc `msg_len` khớp sai → `bad_responses`,
    **không** trả thành công;
  - không có response → retry có giới hạn, hết thì `-ETIMEDOUT`;
  - disconnect/reset trong lúc chờ → `-ENODEV`/`-ENETRESET`, link chuyển DOWN;
  - `client_reset(LINK_RESET)` xoá pending (CNSS restart), txn id vẫn tăng;
  - buffer tĩnh 6400 B trong client, không malloc, đủ segment dữ liệu 6144 B
    cộng TLV/header; mọi độ dài đều kiểm tra.
- `vqeaf_qmi_wait_indication()` chỉ trả packet khi **đã nhận** đúng message ID.
  Indication khác được giao cho handler rồi tiêu thụ; hết deadline là
  `-ETIMEDOUT`. Callback nhận payload chỉ sống trong lúc callback và không được
  gọi lại client.

## WLFW client độc lập (2026-11-24)

`wlfw_client.c` triển khai các message đã xác nhận từ schema công khai:
`IND_REGISTER 0x20`, `FW_READY_IND 0x21`, `CAP 0x24`, `BDF_DOWNLOAD 0x25`,
`CAL_REPORT 0x26`, `CAL_UPDATE 0x29`, `MSA_INFO 0x2D`, `MSA_READY 0x2E`,
`MAC_ADDR 0x33`. Nó kiểm tra result TLV type `0x00`, CAP board ID, thứ tự
state, giới hạn 6144 B/segment và tối đa 5 calibration ID; mọi TLV sai độ dài
đều fail-closed.

Chỉ status `FW_READY` trong response đăng ký hoặc `FW_READY_IND` nhận được mới
đặt `fw_ready`. Indication đến khi client còn `NEW`/`FAILED` không được dùng làm
bằng chứng. `FW_READY` **không** chứng minh BDF CRC, calibration, HTC/WMI,
netdev, RF, association hay IP. `firmware_boot.c` vì vậy có gate `WLFW_READY`
riêng trước `WLAN_READY`.

Client chưa nối với backend `AF_QIPCRTR` thật của Violet. Không có allocation
động, response/indication/endpoint/MAC/calibration/readiness tự tạo.

## Test (all FIXTURE, host-side)
`python scripts/wifi/run_hal_tests.py` →
```
test_wifi_hal: ALL OK
test_qrtr_qmi: ALL OK
test_wlfw_client: ALL OK
test_firmware_boot: ALL OK
test_wifi_firmware: real firmware validated
test_wifi_firmware: ALL OK
```
Các case đã chứng minh: codec roundtrip, packet ngắn → `-EINVAL`, discovery
có server / không server / packet rút gọn, bảng add/drop, header/TLV bounds,
response OK/failure, sai txn, sai độ dài, retry, disconnect/reset, indication
khớp/không khớp; WLFW register→MSA→CAP→BDF→CAL→MAC, BDF 7000 B hai segment,
MSA/FW-ready indication nhận giữa request, status FW_READY, timeout, CAP thiếu,
QMI error 90, result TLV méo, indication ready quá sớm, MAC/input sai và CAL
length vượt TLV/capacity. Boot state machine chặn discovery-only và crash loop.

```
python -m unittest tests.wifi.test_qrtr_qmi tests.wifi.test_firmware_boot \
  tests.wifi.test_wlfw_client
```
→ 8 tests OK.
`python scripts/wifi/run_violet_platform_tests.py` →
`vqeaf_violet_platform: ALL OK`.
Compile riêng WLFW/client boot với `-Wall -Wextra -Werror` → PASS.

## Build
`python scripts/build_kernel_aarch64.py` → AArch64 ELF **711.320 B**, exit 0.
Lưu ý: thay `memmove` bằng vòng lặp trong `qrtr_transport.c` — musl `memmove`
kéo ~900 KB debug info vào ELF freestanding. Đây là lý do kích thước tăng
tạm thời trong quá trình phát triển, đã sửa.

## Trạng thái phần cứng (không đổi)
Đây là **host logic test**, không phải bằng chứng Wi-Fi chạy thật. WLFW `0x45`
vẫn **chưa được modem publish**; `qrtr-lookup 0x45` vẫn `recvfrom` timeout;
`wlan0` vẫn absent. Client này sẽ trả `-ENOENT` đúng như thực tế. Không có
foothold mới cho việc giả service — blocker vẫn là factory data/calibration và
WLFW service thật từ firmware.

## Cargo còn thiếu (không tự làm)
- Gắn IO callback thật (`AF_QIPCRTR` socket trong PID1) cho native kernel/client
  và chạy `discover(0x45)` trên máy để lấy bằng chứng `NEW_SERVER` thật.
- Nối `vqeaf_wlfw_*` vào driver WCN3990 native với dữ liệu firmware/BDF/calibration
  thật; hiện mới có protocol client và host proof.
- Xác minh trên thiết bị được uỷ quyền: WLFW `0x45`, handshake, `wlan0`, RF/scan.
  Không nạp ảnh hay ghi partition trong lượt này.
