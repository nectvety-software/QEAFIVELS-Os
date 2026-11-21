<!--
Nguồn: https://wireless.docs.kernel.org/en/latest/en/users/drivers/ath10k.html
Tài liệu Linux kernel (GPL-2.0), bản chép offline ngày 2026-11-19.
-->

# About ath10k

ath10k is the mac80211 wireless driver for Qualcom Atheros QCA988x family of chips, which support IEEE 802.11ac. It was first included in Linux 3.11-rc1 released on 2013-07-14.

The ath10k driver is located under `drivers/net/wireless/ath/ath10k` directory.

## Mục lục của tài liệu gốc

- Support and reporting bugs (mailing list)
- Sources (git branches, backports)
- Submitting patches
- Board Files
- FAQ
- Architecture: MAC, HTT, WMI, HTC, HIF, debug, tracing, PCI, Copy Engine, BMI, core
- Debug: debug masks, tracing, HTT stats, firmware version, DFS, bitrates, crash dump
- Calibration: calibration data, board data, determining sources, BMI ID
- Firmware API versions 1–6
- Configuration: building, loading modules, hostapd, MU-MIMO, mBSSID
- Coding style, Spectral Scan, Monitor Mode, Mesh Mode, TODO

## Supported Devices

ath10k supports Qualcomm Atheros 802.11ac QCA98xx hw2.0 and QCA6174 based devices. Danh sách đầy đủ trong tài liệu gốc (QCA9880/82, QCA9890/92, QCA9888, QCA9984/9994, QCA6174/A, IPQ4018, ...).

## Not supported

- QCA98xx hw1.0 (ví dụ QCA9880 Version 1 trong TP-Link WDR-7500 v2 / Archer C7 v1.x).
- Thiết bị SDIO hoặc USB (đang phát triển).

## Known bugs/limitations

- Firmware không hỗ trợ associate cùng AP từ hai virtual STA interface (driver in `Failed to add peer ... for VDEV: X`).
- Chưa hỗ trợ packet injection.
- Patch regulatory-domain hack của OpenWRT gây crash firmware.
- TX rate báo 6 mbps do firmware limitation; xem `/sys/kernel/debug/ieee80211/phyX/ath10k/fw_stats`.
- WEP không chạy với AP_VLANs.
- TX speeds kém trên một số chip (QCA6174).

## Liên quan trực tiếp đến WCN3990 / violet

- DT binding `qcom,wcn3990-wifi` (SNOC), 12 IRQ, `memory-region` trỏ tới wlan MSA, cal data/board data — xem `qcom-ath10k-dt-binding.txt`.
- Firmware nằm tại `/lib/firmware/ath10k/WCN3990/hw1.0/`.
- Với SNOC, firmware WLAN (`wlanmdsp.mbn`) do MPSS khởi động và được phục vụ qua `tqftpserv`; driver chờ `WLFW_FW_READY_IND`.
- `bus=snoc,qmi-board-id=...,qmi-chip-id=...,variant=...` được dùng để chọn board data; xem `aarch64-laptops-wifi-README.md` và `wlfwd-README.md`.
