# Violet Device Tree sources (2026-11-23)

## Nguồn đã truy xuất
| Repository | Branch | Commit | Vai trò |
| --- | --- | --- | --- |
| GuaiYiHu/android_kernel_xiaomi_sm6150 | `mkq-mr1` | `54dd13a140754a865867b38000ae7ae96fc43ce4` | SoC DTS/DTSI + dt-bindings |
| Xiaomi-Redmi-Note-7-Pro/android_device_xiaomi_violet | `kang` | `541d7057cdb84658dafacc17afee3756314d5f4c` | device overlay/config |
| mavji7/android_device_xiaomi_violet | `aex` | `e85fad6b103a4f9056c411de8bde50a7842d6224` | đối chiếu |
| RedmiNote7Pro/device_xiaomi_violet | `eleven` | `8e08dc7ddebe93698fc9685781c2f8e2a9a3a014` | đối chiếu |

Manifest SHA256 từng file: `platform/violet/device-tree/manifests/dt-sources.json`.
Cây DTS được tải theo dependency `#include` qua `raw.githubusercontent.com` trên commit ở trên, lưu tại `platform/violet/device-tree/sources/guaiyihu-sm6150/`. `scripts/wifi/fetch_dts_recursive.py` (temp) là công cụ tải; phiên bản đưa vào repo là `scripts/wifi/build_reference_dtb.sh`.

## Kết quả kiểm kê
- **Không có board DTS mang tên violet** trong repo kernel đã kiểm tra; chỉ có `arch/arm64/configs/vendor/violet-perf_defconfig`. Các device repo chỉ chứa `WCNSS_INI_SYMLINK`, `WCNSS_MAC_SYMLINK` trong `device.mk`, không có `.dts`.
- DTB tham chiếu được biên dịch từ **`sm6150.dts`** (SoC board), không phải board violet. File: `platform/violet/device-tree/build/violet-reference.dtb`, SHA256 `ce28d1f4d70cbbd4d6217196f91b78a9d9f2e075e351aa92c06d29d58b3b7f73`, 387.690 B.
- **Không** tạo `violet-live.dtb`: chưa có DTB trích trực tiếp từ `/proc/device-tree` trên máy và xác minh nguồn.

## Tài nguyên WCN3990 xác minh được từ DTS
Node `qcom,icnss@18800000` trong `sm6150.dtsi`:
- `reg`: `0x18800000` size `0x800000` (membase), `0xa0000000` size `0x10000000` (SMMU IOVA), `0xb0000000` size `0x10000` (SMMU IOVA IPA).
- `interrupts`: SPI 414..425 (12 CE IRQ).
- `iommus`: `<&apps_smmu 0x1640 0x1>`.
- `qcom,wlan-msa-memory = <0x100000>`.
- `qcom,wlan-msa-fixed-region = <&wlan_msa_mem>` → `wlan_msa_region@93300000`: `reg = <0 0x93300000 0 0x200000>`, `removed-dma-pool`, `no-map`.
- Regulator supplies: `vdd-cx-mx-supply`, `vdd-1.8-xo-supply`, `vdd-1.3-rfa-supply`, `vdd-3.3-ch0-supply` (phandle tới PM6125/PM6150).

## Còn thiếu / UNKNOWN
- Board-specific violet DTS/DTBO overlay (panel, camera, board-id) chưa có nguồn truy xuất được; không tự suy đoán.
- Clock IDs, reset IDs và regulator enable sequencing: DTS có phandle nhưng chưa có driver primitive trong kernel QEAF; backend báo `UNKNOWN`/`-ENOSYS`, không giả thành công.
- DTB live của thiết bị: chưa trích; cần verb rescue mới nếu muốn đối chiếu.
