# WCN3990 controlled bring-up (2026-11-23)

Trình tự dưới đây là thiết kế có kiểm soát cho `VqeafWcn3990State`
(`kernel/include/vqeaf/wcn3990.h`). Mỗi bước có trạng thái, timeout, error code,
cleanup/rollback. **Không bước nào được bỏ qua hay giả thành công.**

Hợp đồng ABI cần phân biệt: `VqeafWcn3990Device`/`VqeafWcn3990State` là
**ABI 1** và không có timeout primitive. Vòng đời Phase 2 là **ABI 3** (timeout
được giới thiệu từ v2): op trả `-EAGAIN` được retry tới `step_timeout_ms` (mặc định
1000 ms), dừng nếu clock không tiến triển hoặc đạt 1024 attempt, rồi `-ETIMEDOUT` +
unwind ngược thứ tự. Cần provider cấp `now_ms`/`delay_ms`; backend hiện **chưa có**
primitive này nên không tự retry — không giả thời gian/kết quả.

| # | Bước | Trạng thái hiện tại | Error khi thiếu | clean-up |
| --- | --- | --- | --- | --- |
| 1 | Parse + validate DTB | IMPLEMENTED (`vqeaf_violet_probe`, host gate PASS) | `-EINVAL` header / `-ENODEV` node | `memset` census |
| 2 | Xác định resource | IMPLEMENTED (reg/IRQ/SMMU `VERIFIED`) | `-ENOENT` property | — |
| 3 | Check MMIO/DMA ownership | **UNSUPPORTED** (không có ioremap/DMA primitive) | `-ENOSYS` | unmap nếu đã map |
| 4 | Check regulator/clock/reset | **UNSUPPORTED** (DTS có supply phandle, kernel chưa có primitive) | `-ENOSYS` | disable ngược thứ tự |
| 5 | Prepare MSA / reserved-memory | **RESOURCE DONE**: phandle resolve → `0x93300000/0x200000`; prepare vẫn `-ENOSYS` (thiếu primitive) | `-ENOSYS` | release MSA |
| 6 | Start remote processor | **UNSUPPORTED** (`remoteproc` census) | `-ENOSYS` | stop ngược thứ tự |
| 7 | Load firmware đã verify | Host gate PASS (`test_wifi_firmware: real firmware validated`, `wlanmdsp.mbn` sha `4c3d277812fe0496…`); device path `/lib/firmware` | `-ENOENT` | unload |
| 8 | Init QRTR transport | transport existing, regression `test_qrtr_qmi: ALL OK` | `-EAGAIN` timeout | destroy endpoint |
| 9 | Discover QMI/WLFW service | **BLOCKED on live device** (xem bằng chứng) | `-ENOENT`/error 45 | release service |
| 10 | Init WLAN interface | **BLOCKED** (`wlan0` absent) | `-ENODEV` | unregister netdev |

## Bằng chứng live (thiết bị violet, phiên trước, vẫn còn nguyên)

```
cnss-daemon: alive (binderfs mounted)
qrtr-live: service=0x45 ... recv_calls=0 recv_bytes=0
server=NOT_REGISTERED rc=-2 (ENOENT) BLOCKED
pd-listen: msm/modem/wlan_pd via servreg 10:3 result=1 error=45
restart msm/modem/wlan_pd: result 1 error 9
restart root_pd: result 1 error 9
/sys/bus/msm_subsys/devices/subsys1/name: modem
/sys/bus/msm_subsys/devices/subsys1/state: OFFLINING
/sys/bus/msm_subsys/devices/subsys1/restart_level: SYSTEM
/sys/class/remoteproc: ABSENT (kernel dùng msm_subsys)
firmware_class.path: /lib/firmware
```
Không có `wlfw_cal_*` được tạo. Hash firmware khớp file đã stage → **không phải lỗi firmware cũ/thiếu**.

## Phân tích blocker

- WLFW `0x45` không được đăng ký dù QRTR transport chạy (`recv_calls=0`): service
  nằm phía modem/PD, không phải phía QEAF. Không tự đăng ký server `0x45` giả.
- `wlan_pd` trả error 45, `pdr-restart` trả error 9, modem `subsys1` đang `OFFLINING`
  → chuỗi khởi tạo remote processor phía modem chưa hoàn tất. Đây là blocker có thật.
- Backend C mới (bước 1–2) chưa giải quyết được bước 3–6 vì kernel QEAF chưa có
  primitive MMIO/IRQ/DMA/regulator. Không hardcode giá trị để "vượt" blocker.

## Điều kiện dừng an toàn đã implement

- `vqeaf_violet_bind_phase2()` / `vqeaf_violet_bind_wcn3990()` trả `-ENODEV`
  khi parse DT thất bại; sau khi resolve MSA thật thì dừng ở `-ENOSYS` tại
  `map_snoc`/`map_registers` → **không** có device với resource suy đoán.
- Mọi callback phần cứng trả `-ENOSYS` → không caller nào nhầm là thao tác thành công.
- FDT: giới hạn đệ quy 32 cấp, header dùng phép trừ chặn overflow; Phase 2 chỉ nhận
  đúng 12 IRQ; legacy bind từ chối IRQ không liên tục hoặc vượt `UINT32_MAX`.
- Timeout/error code/rollback: Phase 2 retry `-EAGAIN` có deadline, giới hạn clock/attempt
  rồi unwind; steps thiếu primitive dừng ở `-ENOSYS`/`-ENODEV`, không rơi vào success path.
- CE ABI 2: source/destination ring-base đều là register 32-bit; không tồn tại
  base-high và backend không ghi offset `0x10`/`0x12`. `ce_disable` chỉ trả `0`
  khi CE không còn tham chiếu DMA ring. Nếu cleanup lỗi, Phase 2 giữ
  quiesce/resource, không giải phóng IRQ/MSA hay unmap, và từ chối bind mới bằng
  `-EBUSY` tới khi reset.

## Không tuyên bố

Không khẳng định Wi-Fi hoạt động. Chỉ có: build PASS, host tests PASS, DTB validate
PASS, và bằng chứng blocker trên thiết bị thật. Wi-Fi vẫn **BLOCKED**.
