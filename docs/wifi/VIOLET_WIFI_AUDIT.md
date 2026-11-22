# Violet Wi-Fi repository audit (Giai đoạn 1)

Ngày: 2026-11-22. Chỉ audit, không flash/ghi phân vùng.

## 1. Kernel hiện tại
- Kernel đang chạy: `4.14.215-azure-Pixel+` (stock vendor, monolithic).
- `/sys/module` có `wlan`, `cfg80211`, `icnss`; **không có `ath10k`/`ath11k`**.
- `/sys/bus/platform/drivers/icnss/18800000.qcom,icnss` tồn tại.
- Bus xác nhận từ dmesg/DT: WCN3990 là thiết bị **SNOC** (`qcom,wcn3990-wifi`,
  `18800000.qcom,icnss`, `wlan_msa_region@93900000`). Không có bằng chứng PCIe.
- Kernel tự viết (`kernel/`) là freestanding AArch64/x86_64 research, **chưa có
  network stack** (không netdev/sk_buff/IP/TCP). Chưa phải kernel chạy trên máy.

## 2. Phân loại module
| Thành phần | File | Phân loại | Bằng chứng |
| --- | --- | --- | --- |
| WCN3990 resource/ops contract | `kernel/include/vqeaf/wcn3990.h` | **Partial** | enum state + callback enum, không có implementation phần cứng |
| WCN3990 SNOC orchestration | `kernel/drivers/wcn3990_snoc.c` | **Partial** | validate resources → gọi ops/map/irq/firmware/CE/WLFW/netdev; fail-closed; backend do platform cung cấp |
| Phase2 power/CE skeleton | `kernel/drivers/wcn3990_phase2.c`, `wcn3990_phase2.h` | **Partial/Stub** | CE table + ops validation, comment nói rõ "No QMI, WLFW, firmware boot, or network interface" |
| Smoke tests | `tests/wcn3990_driver_smoke.c`, `tests/wcn3990_phase2_smoke.c` | **Mock (test-only)** | dùng callback giả để kiểm tra lifecycle; không phải hardware evidence |
| Native driver doc | `docs/platforms/VIOLET_WCN3990_NATIVE_DRIVER.md` | Partial | tự thừa nhận chưa đầy đủ power/clock/CE/DMA/QRTR/QMI/data path |
| QRTR in PID1 | `platform/violet/qrtr_ns.c` | **Implemented (host)** | name service thật trên QRTR socket; servers report có thật |
| pd-mapper/tqftpserv/rmtfs | `platform/violet/hosttools/src/` | Implemented | static musl, chạy trên thiết bị, log xác nhận |
| cnss-daemon | `build/violet/hosttools/cnss-daemon` (từ ROM) | External binary | chạy thật với binderfs, nhưng im lặng (kernel không có logger) |
| qwlfgw (WLFW 0x45 server thuần C) | `platform/violet/hosttools/src/qwlfgw/qwlfgw.c` | Partial | publish service 0x45, parse QMI cơ bản; chưa có bằng chứng WLFW thật |
| Network stack QEAF kernel | `kernel/` | **Not implemented** | không có netdev/IP/TCP; virtio-net M1 chỉ discovery |
| UI Wi-Fi | `platform/violet/ui/vqeaf_wifi.c`, `vqeaf_pages.c` | Implemented (honest) | scan/connect qua WEXT; khi không có wlan0 trả ENODEV, không giả mạng |

## 3. Điểm đã hardware-verified
- `firmware_mnt` mount từ `/dev/mmcblk0p82`, `wlanmdsp.mbn` hiện diện.
- Modem subsys `ONLINE` sau `wifi up` (`/sys/bus/msm_subsys/devices/subsys1/state`).
- `rmtfs`, `tqftpserv`, `pd-mapper`, `pd-listen` chạy; binderfs và cnss-daemon chạy.
- `qrtr-lookup 0x45` gửi được nhưng `recvfrom` timeout; **không có server 0x45**.
- `wlan0` vắng mặt; modem reset ~40 s sau khi ONLINE.

## 4. Blocker theo thứ tự
1. persist (p74) thiếu `wlan_mac.bin`/calibration thật → `find_factory_mac()` fail.
2. SERVREG listener chưa có bằng chứng `result=0 error=0`.
3. WLFW `0x45` chưa publish → icnss/qcacld không có firmware-ready.
4. Modem reset sau khi ONLINE (độc lập).
5. Kernel tự viết chưa có network stack → không thể coi là driver đầy đủ.

## 5. Nguồn tham khảo bên ngoài (đã truy cập 2026-11-22)
- LineageOS Wiki — *Update firmware on violet*:
  https://wiki.lineageos.org/devices/violet/fw_update/
  (CC BY-SA 3.0). Tóm tắt: firmware India `V12.5.1.0.QFHINXM`, China
  `V12.5.4.0.QFHCNXM`; **không trộn**; xác định SKU từ MIUI version. Không tự flash.
- Device-tree binding `qcom,wcn3990-wifi` (Qualcomm android kernel, đã trích dẫn
  trong `docs/platforms/VIOLET_WCN3990_NATIVE_DRIVER.md`): SNOC + MMIO + CE IRQ.
- Các URL mifirm/xmfirmwareupdater chỉ dùng để đối chiếu nguồn; **không tự tải
  firmware không rõ license/.zip lạ**.

Không có mock nào được giữ làm đường đi thay thế cho phần cứng thật; smoke test
chỉ là test lifecycle với callback giả, ghi rõ trong file nguồn.
