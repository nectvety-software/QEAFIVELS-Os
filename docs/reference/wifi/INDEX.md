# Tài liệu tham khảo Wi‑Fi (offline)

Thư mục này chép các tài liệu **upstream** phục vụ port Wi‑Fi trên violet
(Qualcomm WCN3990 / kernel 4.14 qcacld) và MT689x. Mỗi file giữ nguyên nội dung
nguồn kèm URL và thông tin giấy phép ở đầu file. Ngày chép: 2026-11-19.

## Danh mục

| File | Nguồn | Dùng để làm gì |
| --- | --- | --- |
| `wlfwd-README.md` | `github.com/ScYurii/wlfwd` (GPL-2.0) | Giải thích WLFW QMI 0x45, thứ tự service, `wlfwd`, `pdr-restart`, board data từ persist |
| `tqftpserv-README.md` | `github.com/linux-msm/tqftpserv` (BSD-3-Clause) | TFTP server: modem tải `wlanmdsp.mbn`, log served/missing |
| `pd-mapper-source-notes.md` | `github.com/linux-msm/pd-mapper` (BSD-3-Clause) | Định dạng `*.jsn`, SERVREG 0x40, vì sao cần `mpss_wlan_pd` |
| `qrtr-source-notes.md` | `github.com/linux-msm/qrtr` (BSD-3-Clause) | AF_QIPCRTR, `qrtr-ns`, QMI encode/decode |
| `rmtfs-source-notes.md` | `github.com/linux-msm/rmtfs` (BSD-3-Clause) | Cờ `-o/-P/-r/-s`, tên file EFS, thứ tự shared memory |
| `qcom_pd_mapper.c.txt` | `torvalds/linux` `drivers/soc/qcom/qcom_pd_mapper.c` (GPL-2.0) | Bảng domain chuẩn: instance 180, `tms/pdr_enabled`, `wlan/fw`, `kernel/elf_loader` |
| `pdr_internal.h.txt` | `torvalds/linux` v6.12 `drivers/soc/qcom/pdr_internal.h` (GPL-2.0) | Message ID PDR: `0x20` register listener, `0x21` get domain list, `0x24` restart PD |
| `ath10k-driver.md` | `wireless.docs.kernel.org/.../ath10k.html` (GPL-2.0) | Tổng quan driver ath10k, supported devices, known bugs, firmware/calibration |
| `qcom-ath10k-dt-binding.txt` | `kernel.org/doc/Documentation/devicetree/bindings/...` | DT binding `qcom,wcn3990-wifi`, MSA, board data,Supply/quirk |
| `aarch64-laptops-wifi-README.md` | `aarch64-laptops/build` `misc/lenovo-yoga-c630/wifi` | Hướng dẫn thực tế: patch ath10k, dựng qrtr/qmic/pd-mapper/tqftpserv/rmtfs, firmware path |
| `*-LICENSE.txt` | các repo linux-msm | Giữ license kèm source notes |

`deepwiki.com/aarch64-laptops/build/6-device-enablement-guides` bị rate-limit
(HTTP 429) lúc tải; nội dung chính của trang đó nằm trong
`aarch64-laptops-wifi-README.md` (file nguồn) nên không bị thiếu.

## Tài liệu nội bộ liên quan

- `docs/platforms/VIOLET_WIFI_BRINGUP.md` — kiến trúc host services của QEAF, verb rescue, bằng chứng đo trên máy.
- `D:\os-system\NectvetyOS\_research\violet-sm6150-mainline\docs\CHECKLIST.md` (W1–W7), `PROGRESS.md`, `STATUS.md` — log thực đo của port mainline cùng máy.
- `D:\os-system\hakios-xnu-kernel\docs\driver-review\wifi-bringup.md`, `wifi-stack.md` — review bảo mật driver Wi‑Fi tự viết.

## Kết luận ngắn gọn từ tài liệu

1. `qrtr-ns` → `pd-mapper` → `tqftpserv` + `rmtfs` → modem → `wlfwd` → `/dev/wlan`.
2. `pd-mapper` phải có `msm/modem/wlan_pd` (instance 180) và `tms/pdr_enabled`; nếu không modem treo ~40 s.
3. `wlanmdsp.mbn` do MPSS tải qua `tqftpserv`; WLFW 0x45 chỉ xuất hiện sau đó, ath10k/qcacld mới có `wlan0`.
4. Trên mainline cần khởi động cả modem **lẫn** WLAN remoteproc; trên kernel 4.14 stock WLAN PD nằm trong modem.
