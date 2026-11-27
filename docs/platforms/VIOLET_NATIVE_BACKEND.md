# Violet Native Platform Backend (2026-11-23)

## Kiến trúc đã nối
`kernel/drivers/wifi/wcn3990/violet_platform.c` (+ `kernel/include/vqeaf/violet_platform.h`) là provider duy nhất đọc DTB và cấp resource cho các driver hiện có:

- **FDT provider**: `vqeaf_violet_probe()` parse header, strings, node `qcom,icnss` (node WLAN thật trong SM6150 DTS, không phải tên `qcom,wcn3990-wifi` suy đoán). Đọc `reg`, `interrupts`, `qcom,wlan-msa-memory`, `iommus`.
- **MMIO/census**: 3 vùng reg từ `reg` (membase + 2 SMMU IOVA) được đánh dấu `VERIFIED` vì có trong DTS. Không tự map thanh ghi.
- **IRQ/GIC**: 12 SPI 414..425 từ `interrupts`; `VERIFIED` ở mức resource.
- **IOMMU/SMMU**: `<&apps_smmu 0x1640 0x1>` → bỏ cell phandle, lưu id `0x1640`/flags `0x1`, `VERIFIED` ở mức resource.
- **Reserved-memory**: resolve `qcom,wlan-msa-fixed-region` → phandle → node `wlan_msa_region@93300000`, đọc `reg` theo `#address-cells/#size-cells` của parent (2+2) → MSA `0x93300000` size `0x200000`; `memory = VERIFIED`. Kiểm tra `size >= qcom,wlan-msa-memory` (`0x100000`) và **không chồng lấn** vùng MMIO `0x18800000/0x800000` trước khi chấp nhận. Không hardcode.
- **Regulator resources**: đọc 4 supply (`vdd-cx-mx`, `vdd-1.8-xo`, `vdd-1.3-rfa`, `vdd-3.3-ch0`) kèm phandle_from_DT → `regulator_count=4`. Chỉ discovery; enable vẫn `UNSUPPORTED`.
- **Interrupt parent**: nếu node có `interrupt-parent`, resolve phandle và đọc `#interrupt-cells` để parse IRQ đúng số cell; nếu không có thì dùng mặc định GIC_SPI 3 cell (đúng với DTS hiện tại).
- **Regulator/clock/reset/DMA/remoteproc**: `UNSUPPORTED` — DTS có phandle supply nhưng kernel QEAF chưa có primitive `regulator_enable`, `clk_prepare`, `reset_control`, `dma_alloc`, `remoteproc_start`. Backend **không giả** các thao tác này.

## Kết nối driver
- `vqeaf_violet_bind_phase2()` → điền `VqeafWcn3990P2Resources` + ops từ DTB, gọi `vqeaf_wcn3990_phase2_bind()`.
- `vqeaf_violet_bind_wcn3990()` → điền `VqeafWcn3990Device` + ops, gọi `vqeaf_wcn3990_bind()`.
- Mọi callback phần cứng là `op2/op0/opirq/...` trả **`-ENOSYS`** kèm census `UNSUPPORTED`. Đây không phải callback giả: nó không bao giờ trả success hay thay đổi trạng thái phần cứng.
- `wcn3990_snoc.c` đã chấp nhận compatible **`qcom,icnss`** (node WLAN thật trong DTS) bên cạnh binding `qcom,wcn3990-wifi`; không đổi giá trị đọc được từ DT để qua validation.

## Điều kiện dừng an toàn
Sau khi MSA resolved, bind vẫn dừng có kiểm soát tại `map_snoc`/`map_registers` với **`-ENOSYS`**, state `FAILED`, error `-ENOSYS` → resource discovery xong nhưng thiếu primitive phần cứng. `-ENODEV` chỉ khi parse DT thất bại. Không hardcode MSA (giá trị thật `0x93300000/0x200000` đọc từ phandle).

## Trạng thái
| Thành phần | Trạng thái |
| --- | --- |
| FDT parse + resource census | IMPLEMENTED, host test PASS; giới hạn depth 32 và kiểm tra overflow/header |
| Legacy CE IRQ range | IMPLEMENTED: chỉ chấp nhận IRQ liên tục, từ chối wrap/rải rác trước khi bind |
| Kết nối phase2/wcn3990 | IMPLEMENTED (fail-closed) |
| MMIO map / regulator / clock / reset / IRQ / DMA / remoteproc | UNSUPPORTED (không có primitive kernel) |
| Phandle MSA resolution | IMPLEMENTED (`0x93300000/0x200000` + bounds check, host test PASS) |
| Regulator resource discovery | IMPLEMENTED (4 supplies/phandles; enable UNSUPPORTED) |
| Timeout phase2 (ABI 3, giới thiệu từ v2) | IMPLEMENTED: `-EAGAIN` retry đến deadline, dừng khi clock không tiến triển hoặc đạt 1024 attempt rồi `-ETIMEDOUT`; wrap `uint64` an toàn. Backend chưa có clock/delay primitive → `now_ms/delay_ms = NULL`, nên `-EAGAIN` được propagate, không loop vô hạn |
| CE ring map + cleanup/quiesce (CE ABI 2, phase2 ABI 3) | IMPLEMENTED: source/destination ring-base đều 32-bit, không ghi base-high; lỗi `ce_disable` giữ ring/resource và chặn bind mới bằng `-EBUSY`. Fixture PASS gồm biên 4 GiB, persistent MMIO và teardown CE11 thành công trước CE10 lỗi |
| WLFW 0x45 / wlan0 | BLOCKED như trước, không đổi |

## Lỗi đã phát hiện nhờ bật assert thật
Các test C build bằng Zig trước đây dùng `assert()` bị tắt (no-op) → báo "ALL OK" dù không kiểm tra. Sau khi thay bằng `tests/vqeaf_assert.h` (macro abort thật), các lỗi sau lộ ra và đã sửa:
- `wifi_device.c`: buffer path khai báo `const` rồi ghi qua cast → Zig tối ưu bỏ phép ghi, mọi path thành `///`.
- `violet_platform.c`: trỏ vùng node sai offset; walker đệ quy truyền nhầm cursor; đọc `reg` với 4 cell thay vì 1/1 của parent; đọc phandle `iommus` làm SMMU ID.
- `qmi_service.c`: recv `len=0` bị đếm thành response sai.
- Test isolation: bảng QRTR server và mock firmware IO không reset giữa các ca.
- `wcn3990_snoc.c`: từ chối compatible `qcom,icnss` thật.

Không flash, không ghi phân vùng trong đợt này.
