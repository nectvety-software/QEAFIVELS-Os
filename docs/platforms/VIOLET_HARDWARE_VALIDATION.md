# Violet hardware validation report (2026-11-23)

## Phạm vi
Chỉ đọc/xác thực nguồn và build host. **Không ghi phân vùng, không flash, không EDL.**
Dữ liệu thiết bị trong báo cáo là kết quả đọc từ phiên trước (bằng chứng lưu trong
`docs/STATUS.md` và `docs/platforms/VIOLET_WIFI_BRINGUP.md`).

## Ma trận xác thực

| Tài nguyên | Nguồn | Giá trị / kết quả | Mức tin cậy |
| --- | --- | --- | --- |
| SoC compatible | `sm6150.dts` (GuaiYiHu `54dd13a`) | `qcom,sm6150`, DTB `nodes=2628`, `phandles=1215` | VERIFIED (DTB biên dịch) |
| WLAN platform node | `sm6150.dtsi` | `qcom,icnss@18800000` | VERIFIED (DTS) |
| ICNSS membase | `reg[0]` | `0x18800000` size `0x800000` | VERIFIED (DTS) |
| SMMU IOVA | `reg[1]`, `reg[2]` | `0xa0000000`/`0x10000000`, `0xb0000000`/`0x10000` | VERIFIED (DTS) |
| CE interrupts | `interrupts` | SPI 414–425 (12 IRQ) | VERIFIED (DTS + host test) |
| IOMMU | `iommus` | `<&apps_smmu 0x1640 0x1>` | VERIFIED (DTS) |
| MSA requested | `qcom,wlan-msa-memory` | `0x100000` | VERIFIED (DTS) |
| MSA region | `wlan_msa_region@93300000` | `0x0 0x93300000 0 0x200000`, `removed-dma-pool`, `no-map` | VERIFIED (DTS + phandle resolve trong C) |
| Regulator supplies | `vdd-cx-mx/1.8-xo/1.3-rfa/3.3-ch0-supply` | phandle PM6150/PM6125 | VERIFIED phandle, **UNSUPPORTED primitive** |
| Clock / reset IDs | — | KHÔNG có trong nguồn đã kiểm tra | **UNKNOWN** |
| Board violet DTS/DTBO | 4 repo đã kiểm tra | KHÔNG có (chỉ defconfig) | **UNKNOWN** |
| DTB live (`/proc/device-tree`) | thiết bị | chưa trích | **UNKNOWN** |

## Kết quả build/test host

| Gate | Kết quả |
| --- | --- |
| `scripts/wifi/build_reference_dtb.sh` (cpp + dtc) | `violet-reference.dtb` 387.690 B, sha `ce28d1f4…f73` |
| `scripts/wifi/validate_fdt.py` | header OK, 2628 nodes, 1215 phandles, `verdict=PARTIAL` (`wcn3990_nodes=[//soc/bt_wcn3990]`, resources UNKNOWN do DTB không phải board violet) |
| `tests/test_fdt_validate.py` | 7/7 PASS (header, property, phandle, address translation, reserved-memory, bad-header reject, missing resource); đã sửa lỗi nested-node property leak trong validator |
| `scripts/wifi/run_wcn3990_state_tests.py` | rollback đúng thứ tự ngược ở 2 điểm dừng (`MRCPSpcrU`, `MRCPSIIIiispcrU`) |
| `tests/vqeaf_assert.h` | thay `assert()` no-op của Zig bằng macro abort thật cho mọi test C do Zig build; sau khi bật đã phát hiện và sửa 6 lỗi bị che giấu (xem `VIOLET_NATIVE_BACKEND.md`) |
| `run_wcn3990_state_tests.py` (v2) | 4 ca PASS: rollback 2 điểm, timeout `-ETIMEDOUT` sau 3 lần thử có giới hạn, retry `-EAGAIN` rồi fail có kiểm soát |
| Backend resource census | regulator 4 supplies + phandle từ DT, MSA bounds/overlap check, IRQ parent cells — host gate PASS |
| `scripts/wifi/run_violet_platform_tests.py` | `vqeaf_violet_platform: ALL OK` (reg/IRQ/SMMU đúng, bind fail-closed `-ENODEV`) |
| `scripts/wifi/run_hal_tests.py` | 5/5 ALL OK (incl. `test_wifi_firmware: real firmware validated`) |
| `scripts/build_kernel_aarch64.py` | linked `vqeaf-kernel-aarch64.elf` 465.176 B (có `violet_platform.c`) |
| CTest (`build-native`) | 15/15 Passed |

`verdict=PARTIAL` của validator là **đúng**: DTB tham chiếu từ `sm6150.dts` chứa node
`qcom,icnss` (được backend C đọc), nhưng không chứa board violet overlay nên resource
WCN3990 theo tên `qcom,wcn3990-wifi` vẫn báo UNKNOWN — không tự đổi tên để có vẻ PASS.

## Chưa xác nhận trên điện thoại thật

- DTB đang chạy trên máy có khớp `violet-reference.dtb` không (chưa trích `/proc/device-tree`).
- MMIO map, regulator/clock/reset enable, MSA reserved-memory, DMA, SMMU attach —
  backend báo UNSUPPORTED, chưa chạy trên hardware.
- Remote processor lifecycle; WLFW `0x45`; `wlan0`; quét SSID thật — vẫn BLOCKED với
  bằng chứng ở `VIOLET_WCN3990_BRINGUP.md`.
- Camera/sensor/audio/GNSS/Bluetooth — ngoài phạm vi đợt này.

## Kết luận
Đạt tiêu chí 1, 2, 3, 4, 6 (nghĩa hạn: "kết nối backend" = provider đã nối với driver
hiện có nhưng fail-closed). **Chưa đạt** tiêu chí 5 theo nghĩa firmware boot chạy thật
(vẫn block bởi modem PD), và tiêu chí 7 là báo cáo blocker này. Ưu tiên độ chính xác
phần cứng hơn báo cáo hoàn thành.


## Cập nhật tích hợp MRE → QEAFIVELS (2026-11-23)

Đã tích hợp source-level các module FDT/Violet discovery, MMU, GICv2/v3 và Generic Timer qua `kernel/platform/aarch64/violet_*`. Có bootstrap API riêng; không thay đường QEMU-virt.

Kết quả kiểm thử mới:
- `vqeaf_violet_boot_probe_test`: PASS; xác nhận reference DTB không được phép kích hoạt PMM/MMU/GIC/timer.
- PMM descriptor builder: PASS với memory map tổng hợp, kiểm tra loại trừ kernel/DTB/reserved-memory và từ chối RAM vượt 4 GiB.
- CTest MinGW: 16/16 PASS.
- `scripts/build_kernel_aarch64.py`: AArch64 ELF liên kết thành công, 571.168 bytes; không có undefined symbols hoặc relocations.

**Không phải kiểm thử phần cứng.** Reference DTB vẫn có board identity false, memory regions 0, dynamic reservations 7 và MMU gate unsupported. ELF hiện tại vẫn link ở QEMU address `0x40000000`, không phải Violet load address. Boot entry chưa nhận/xác minh DTB handoff của máy.

ADB/Fastboot không phát hiện điện thoại ở lần kiểm tra gần nhất. Không tạo boot.img, không flash, không ghi hoặc xóa bất kỳ phân vùng nào. XBL/ABL/bootloader và boot chain được giữ nguyên.
