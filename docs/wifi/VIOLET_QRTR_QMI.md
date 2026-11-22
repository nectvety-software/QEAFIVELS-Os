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
| `kernel/include/vqeaf/qmi_service.h` | header/TLV, client state, pending table |
| `kernel/drivers/wifi/wcn3990/qmi_service.c` | txn id, matching, timeout, retry, reset |
| `tests/wifi/gen_qrtr_qmi_fixtures.py` | tạo wire packet FIXTURE |
| `tests/wifi/test_qrtr_qmi.c` | test protocol, mọi nhánh lỗi |
| `tests/wifi/test_qrtr_qmi.py` | wrapper unittest + guard |

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
  - indication bị consume và đếm riêng, không nhầm là response;
  - response sai `txn_id`/`msg_id`/`type` hoặc `msg_len` khớp sai → `bad_responses`,
    **không** trả thành công;
  - không có response → retry có giới hạn, hết thì `-ETIMEDOUT`;
  - disconnect/reset trong lúc chờ → `-ENODEV`/`-ENETRESET`, link chuyển DOWN;
  - `client_reset(LINK_RESET)` xoá pending (CNSS restart), txn id vẫn tăng;
  - buffer tĩnh 512 B trong client, không malloc, mọi độ dài đều kiểm tra.

## Test (all FIXTURE, host-side)
`python scripts/wifi/run_hal_tests.py` →
```
test_wifi_hal: ALL OK
test_qrtr_qmi: ALL OK
```
Các case đã chứng minh: codec roundtrip, packet ngắn → `-EINVAL`, discovery
có server / không server / packet rút gọn, bảng add/drop, header/TLV bounds,
response OK, response failure (`result=1,error=94`), sai txn, sai độ dài,
firmware im lặng + 2 retry (`sends==3`), QRTR disconnect, CNSS reset + indication.

`python -m unittest tests.wifi.test_qrtr_qmi` → 4 tests OK.

## Build
`python scripts/build_kernel_aarch64.py` → AArch64 ELF **421.056 B**.
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
- Gắn IO callback thật (AF_QIPCRTR socket trong PID1) và chạy `discover(0x45)`
  trên máy để lấy bằng chứng NEW_SERVER thật.
- QMI WLFW message IDs cụ thể (`wlfw` service) cần đối chiếu với header firmware
  thật trước khi gửi request có payload.
