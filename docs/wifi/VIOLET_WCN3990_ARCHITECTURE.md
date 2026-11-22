# Violet WCN3990 hardware abstraction (Giai đoạn 3)

## Nguyên tắc
Không hardcode địa chỉ thanh ghi, GPIO, IRQ hay DMA. Mọi resource được đọc từ
DTB thật do platform cung cấp khi boot. Không tạo module trùng: power/clock/
regulator/MSA/IRQ/DMA/CE lifecycle đã có trong `kernel/drivers/wcn3990_phase2.c`
và contract `wcn3990_phase2.h`; HAL mới chỉ bổ sung đúng phần còn thiếu.

## Các file thêm/sửa
| File | Vai trò | Trạng thái |
| --- | --- | --- |
| `kernel/include/vqeaf/wifi_device.h` | kiểu resource + API discovery DTB | Implemented |
| `kernel/include/vqeaf/wifi_firmware.h` | firmware IO/lifecycle contract | Implemented |
| `kernel/drivers/wifi/wcn3990/wifi_device.c` | parser FDT thật (BE32, token, strings block) | Implemented |
| `kernel/drivers/wifi/wcn3990/wifi_firmware.c` | load/validate/hash firmware, buffer do caller giữ | Implemented |
| `scripts/wifi/run_hal_tests.py` | build + chạy C test trên host | Implemented |
| `tests/wifi/gen_dtb_fixture.py` | tạo DTB FIXTURE đúng format | Test-only |
| `tests/wifi/test_wifi_hal.c` | test discovery/firmware/error path | Test-only |
| `tests/wifi/test_wifi_hal.py` | wrapper unittest + static guard | Test-only |

`wifi_bus.h` được giữ làm contract cho tầng map MMIO; implementation nằm ở
ops `map_snoc/unmap_snoc` của phase2 — **không tạo driver map trùng**.
`wifi_power.h`, `wifi_irq.h`, `wifi_dma.h`, `qrtr_transport.h`, `qmi_service.h`
**không tạo stub rỗng** (sẽ vi phạm "Stub"), chúng thuộc phase2 (power/irq/dma)
và Giai đoạn 4 (QRTR/QMI). Ghi chú rõ để tránh hiểu nhầm.

## Resource được xác nhận từ DTB (FIXTURE, mirror thiết bị thật)
- `compatible = "qcom,wcn3990-wifi"` → bus `snoc` (theo device-tree binding
  `qcom,wcn3990-wifi`, đã trích trong `docs/wifi/VIOLET_WIFI_AUDIT.md`).
- `reg` (với `#address-cells=2`, `#size-cells=2` từ parent):
  MMIO `0x18800000..0x19000000`.
- `interrupts` (với `#interrupt-cells=3`, GIC SPI `<type,irq,flags>`):
  12 CE IRQ, irq[0]=130.
- reserved-memory node `compatible = "qcom,wcnss-msa"` → MSA `0x93900000`,
  size `0x200000`.
- Nếu thiếu property → trả `0/unknown`, **không suy diễn giá trị**.

Parser kiểm tra biên mọi token/attribute; blob sai magic hoặc cắt ngắn → lỗi
âm, fail-closed. Không `strcpy/sprintf`; substring search tự implement vì
freestanding không có libc `strstr`.

## Firmware lifecycle
`vqeaf_wifi_firmware_load()` đọc qua callback (kernel: VFS; host: musl) vào
buffer do caller cấp — **không malloc**, không ghi storage. Từ chối blob rỗng,
blob vượt `max_bytes`, lỗi đọc được propagate nguyên vẹn; hash FNV-1a thật.
Không bao giờ tạo firmware/calibration/MAC giả, không log nội dung file.

## Build & test
- `python scripts/wifi/run_hal_tests.py` → `test_wifi_hal: ALL OK`
  (zig cc host, fixture `FIXTURE_violet_wifi.dtb`).
- `python scripts/build_kernel_aarch64.py` → link AArch64 ELF 392.488 B
  (có wifi_device.o + wifi_firmware.o, không symbol libc bị thiếu).
- `python -m unittest tests.wifi.test_wifi_hal` → 3 tests OK.

Đây là **host logic test**, không phải bằng chứng phần cứng. Không có WLFW
`0x45`, `wlan0` vắng mặt như cũ; HAL chỉ làm đúng lớp resource/firmware, chưa
boot Wi-Fi thật.

## Cargo cần phần cứng thật (chưa làm)
Đọc DTB từ boot-format thật trên máy, đối chiếu reg/IRQ với `/sys/bus/platform/
devices/18800000.qcom,icnss`, nạp `wlanmdsp.mbn` thật qua ops của platform.
Không tự flash/ghi phân vùng.
