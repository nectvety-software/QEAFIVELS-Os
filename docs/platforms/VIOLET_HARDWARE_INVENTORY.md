# VIOLET — Hardware Inventory (Phase 1)

Thiết bị: Xiaomi Redmi Note 7 Pro (`violet`), model `M1901F7S` / `M1901F7BE`.
Ngày lập: 2026-11-22. Phạm vi: **chỉ đọc** — không ghi/flash phân vùng nào.

Tài liệu này thay thế phần "hardware inventory" rời rạc trong `docs/VIOLET_HARDWARE.md`
và `docs/DRIVERS_VIOLET.md` bằng một bảng duy nhất theo schema Phase 1. Mọi dòng
đều ghi mức bằng chứng; **không có giá trị nào được suy đoán**.

## 0. Mức bằng chứng (dùng xuyên suốt tài liệu)

| Mã | Ý nghĩa |
| --- | --- |
| **E3** | Đo trực tiếp trên máy này: log rescue/`HOMOLOG.TXT`/`LASTBOOT.TXT` của chính lần boot này, hoặc sysfs đọc qua rescue |
| **E2** | Đọc từ dump/partition của chính máy này (qua rescue, read-only) |
| **E1** | Tài liệu nguồn bên ngoài đã kiểm chứng (LineageOS wiki, Xiaomi OSS branch, DT binding) |
| **E0** | Chưa có bằng chứng — **không được dùng để viết driver**, chỉ liệt kê ở "câu hỏi mở" |

Nguồn log dùng lại: `build/violet-homolog-last.txt` (HOMOLOG.TXT 20.262 B của lần
boot 2026-11-22), `LASTBOOT.TXT` (2.066 B), `PSTORE.TXT`, `DEVINFO.TXT`.

## 1. Danh tính board (đã giải quyết mâu thuẫn cũ)

`docs/VIOLET_HARDWARE.md` từng ghi "SDM660 hay SM6150?" — đã giải quyết:

| Thuộc tính | Giá trị | Bằng chứng |
| --- | --- | --- |
| SoC | Qualcomm **SM6150** Snapdragon 675 | E1: LineageOS wiki + `android_device_xiaomi_violet`; E1: Xiaomi OSS `violet-q-oss` = `LA.UM.8.9.r1-04400-SM6xx.0-1`, `violet-p-oss` = `LA.UM.7.9.r1-03500-sm6150.0` |
| CPU | Kryo 460: 2×2.0 GHz Gold + 6×1.7 GHz Silver, arm64 | E1 |
| GPU | Adreno 612 | E1 |
| RAM | 4/6 GB LPDDR4X | E1 (chưa đọc dung lượng thật trên máy này → E0 cho con số cụ thể) |
| Storage | 64/128 GB eMMC 5.1 (LineageOS), kernel liệt kê `mmcblk0` | E1 + E3: `dev: /dev/mmcblk0p77 is a block device` |
| Kernel đang chạy (stock) | `4.14.215-azure-Pixel+` | E3 (kernel ring) |
| OS tối thiểu trong repo | `QEAFIVELS OS 0.3.0`, PID 1 userspace trên kernel stock | E3: `boot: QEAFIVELS OS 0.3.0 init on violet (PID 1)` |

**Màn hình:** panel native **1080×2340** (19.5:9, 6.3", IPS LCD). Con số `240×520`
trong repo là **canvas logic nội bộ** (`SCREEN.RAW` 240×520 RGB565, phóng 3× thành
720×1560), **không phải** độ phân giải panel. Không có "240×2340".

## 2. Kiểm kê theo thành phần

Các cột `IRQ / GPIO / Regulator / Clock / Reset` để trống = **chưa đọc được Device
Tree trên host này** (xem §5). Không được điền bằng phỏng đoán.

| # | Thành phần | Identifier đã thấy | Bus / địa chỉ | Firmware | Nguồn driver (dự kiến) | Trạng thái |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | SoC / GIC / timer | SM6150 (ARMv8.2-A, GICv3) | — | — | OSS `violet-q-oss` `drivers/irqchip`, `arch/arm64` | **E0 cho địa chỉ** — chưa có bring-up độc lập |
| 2 | Clock / RPMh | rpmh, gcc-sm6150 | SPMI/RPMh | — | OSS `drivers/clk/qcom` | E0 |
| 3 | Regulator | pm6150 / pm6150l (RPMh) | SPMI | — | OSS `drivers/regulator/qcom-rpmh-regulator.c` | E1 (tên part), E0 (rail list) |
| 4 | PMIC + RTC | `qcom,pm6150@0` + `qcom,pm6150_rtc` | SPMI `c440000.qcom,spmi` | — | OSS `qpnp-rtc` | **E3**: RTC set clock `1970-01-11 02:40:26 UTC` |
| 5 | Watchdog | không có thiết bị | — | — | — | **E3**: `watchdog: no watchdog device (No such file or directory)` → không có driver watchdog trong kernel này |
| 6 | Display (SDE/DSI) | `msm_drm`, connector 28 = **T16**, crtc 107 | DSI | panel init seq (chưa có) | OSS `techpack/display`, `drivers/gpu/drm/msm` | **E3**: `connector 28 connected, 1080x2340`, mode `1080x2340x60x183257`, dumb buffer pitch **4352 B** (1088 px), `DIRTYFB unsupported` → scanout liên tục. Chỉ `/dev/dri/card0`, **không có fbdev** |
| 7 | Panel | `dsi_nt36672a_shenchao_vid_display` (Novatek NT36672A, video mode) | DSI | init sequence trong kernel stock | phải port sang QEAF panel driver | E3 (tên + mode), E0 (init seq, điện áp) |
| 8 | Backlight | `/sys/class/backlight/backlight`, 85% | — | — | LED class (WLED/PMIC?) | **E3**: đặt được 85%. E0: rail/GPIO |
| 9 | Touch | **NVTCapacitiveTouchScreen** (Novatek) | evdev `/dev/input/event1` | — | `drivers/input/touchscreen/nt36xxx` (OSS) | **E3**: `x[0..1079] y[0..2339] multitouch`; LVGL đã bind |
| 10 | Buttons / power key | `gpio-keys` | evdev `/dev/input/event0` (power), `event2` | — | OSS `drivers/input/keyboard/gpio_keys` | **E3**: `power: opened /dev/input/event0`, `power: added /dev/input/event1/event2` |
| 11 | Storage controller | `mmcblk0` (eMMC), 122.142.720 KiB tổng | SDHC | — | OSS `drivers/mmc/host/sdhci-msm.c` | **E3/E2**: partition table đọc được; `ufs_phy_gdsc: disabling` xuất hiện nhưng **không có `ufshc`** → vẫn là eMMC. Cần xác nhận bằng DT |
| 12 | USB | `dwc3` UDC | `a600000.dwc3` | — | OSS `drivers/usb/dwc3` | **E3**: `usb: mass storage bound to a600000.dwc3`; UDC có thể đăng ký muộn → cần retry (đã xử lý trong `usb_lun.c`) |
| 13 | Wi-Fi | WCN3990 (integrated, **SNOC**, không phải PCIe) | `18800000.qcom,icnss`; MSA `wlan_msa_region@93900000` | `wlanmdsp.mbn`, `BDWLAN.*`, `regdb.bin` | OSS `icnss` + `qcacld`; mainline `ath10k` qmi | **E3**: `icnss` bind, `/dev/wlan` (500:0), `wlan0` **vắng mặt**; modem ONLINE rồi reset ~40 s (xem §4) |
| 14 | Bluetooth | WCN3990 BT, firmware ở p38 | SMD/UART (chưa chốt) | `BTFM.bin` (p38, vfat ro) | OSS `hci_qca`/`hci_smd` | **E3**: `bluetooth: p38 firmware available`; `/sys/class/bluetooth` rỗng → BLOCKED |
| 15 | Audio codec | **WCD937x** (LPI/SoundWire) | `lpi_pinctrl@62b40000`, `rx/tx_swr_clk_data`, SLIM `62e40000.slim` | — | OSS `techpack/audio` | **E3**: nhiều dòng `lpi_wcd937x_reset_active ... deferring probe`; chưa có soundcard |
| 16 | Audio amp | **TAS2563** | i2c `3-004c` | `tas2563_uCDSP.bin` | OSS `sound/soc/codecs/tas2563` | **E2**: "device not powered", 0 soundcard |
| 17 | ADSP (lpass) | `62400000.qcom,lpass` | PIL/glink | `ADSP.*` (30 file) | OSS `remoteproc` + `q6` | **E3**: `adsp: Brought out of reset` |
| 18 | CDSP (turing) | `8300000.qcom,turing` | PIL/glink | `CDSP.*` (10 file) | OSS | **E3**: `cdsp: Brought out of reset`, `CDSP request manager driver probe called` |
| 19 | Venus (video) | `aae0000.qcom,venus` | PIL | `VENUS.*` | OSS `venus` | **E3**: `venus: Brought out of reset` |
| 20 | IPA | `rmnet_ipa3` | — | `ipa_fws.mdt` | OSS `drivers/net/ipa` | **E3**: `rmnet_ipa3 started initialization`; userspace `/dev/ipa` **không có** |
| 21 | Camera ISP/ICP | `cam-isp` (v4l-subdev1), `cam-icp` (14), `cam-csiphy` ×4, `cam-jpeg`, `cam-cpas`, `cam-lrme` | V4L2 subdev | `CAMERA_ICP.elf` | V4L2 UAPI (không cần HAL) | **E3**: `CAM_QUERY_CAP` OK, IFE 1.7.0, IQMU handles; `CAM_ACQUIRE_DEV` ISP OK (errno 22 với sensor/ICP) |
| 22 | Camera sensor | 3 subdev sensor (`cam-sensor-driver` 7/8/9) | CSI/I2C | sensor mode tables (E0) | OSS sensor drivers | **E3** node tồn tại; **E0** model/sensor ID → không được đoán |
| 23 | Rotator | `sde_rotator ae00000.qcom,mdss_rotator` | — | — | OSS | **E3**: `SDE v4l2 rotator probe success` |
| 24 | Sensor (accel/gyro/prox/light/mag) | LineageOS liệt kê đủ 5 loại | I2C/SPI (chưa rõ) | SLPI (chưa xác minh) | OSS `drivers/iio` | **E0** — chưa có node/ID trên lần boot này; có `i2c_geni 88c000.i2c: i2c error :-107` lặp lại |
| 25 | Fingerprint | goodix/fpc **không có node** | — | `goodixfp.b0*` trong vendor | — | **E3**: `/sys/class/misc` không có node → UNSUPPORTED |
| 26 | GNSS | modem subsystem | QRTR/QMI | từ `NON-HLOS.bin` | OSS `gnss`/`qmi` | **E0** cho driver; modem PIL đã boot được nhưng không có service layer |
| 27 | Haptics / IR blaster / FM | LineageOS có trong spec | — | — | — | **E0** |
| 28 | Thermals | `thermal` (chưa đọc) | — | — | OSS `thermal/qcom` | **E0**; `diag` verb có census nhưng chưa chạy lần này |
| 29 | Security / AVB | `vbmeta` tồn tại, p68 `devinfo` | — | — | — | E2: boot ảnh không ký vẫn chạy → trạng thái verify **chưa characterize** |

## 3. Bản đồ phân vùng (E2/E3, read-only)

Từ `LASTBOOT.TXT` của chính máy (chỉ mount ro, không ghi):

| Phân vùng | Vai trò | Bằng chứng |
| --- | --- | --- |
| `mmcblk0p14` | `fsc` | `hostsvc: rmtfs partition fsc -> /dev/mmcblk0p14 (read-only)` |
| `mmcblk0p38` | `bluetooth` (VFAT, BT firmware) | `wifi: mounted /dev/mmcblk0p38 on /vendor/bt_firmware (vfat, ro)` |
| `mmcblk0p62` | `fsg` | rmtfs |
| `mmcblk0p64` / `p65` | `modemst1` / `modemst2` | rmtfs |
| `mmcblk0p74` | `persist` (ext4, MAC/calibration) | `wifi: mounted ... on /mnt/vendor/persist (ext4, ro)` |
| `mmcblk0p76` | `boot` | E2 (by-name) |
| `mmcblk0p77` | `recovery` — nơi chứa OS/appstore | `dev: /dev/mmcblk0p77 is a block device`; `appstore: seq 51, 17 app(s)` |
| `mmcblk0p82` | `modem` / `firmware_mnt` (VFAT) | `wifi: mounted /dev/mmcblk0p82 on /vendor/firmware_mnt (vfat, ro)` |
| `mmcblk0p86` | `/vendor` (ext4) | `wifi: mounted /dev/mmcblk0p86 on /vendor (ext4, ro)` |

`HOMOLOG.TXT` cũng ghi: `journal: previous boot left a record (2066 bytes, boots=673)`
— bộ đếm boot nằm trong p77 journal (không phải RTC).

## 4. Sự kiện phần cứng đã quan sát (không phải suy đoán)

- **Modem PIL boot làm reset SoC sau ~40 s** nếu không có service layer. Đã xử lý
  bằng cách mặc định *không* boot modem; `wifi up` mới boot và có cảnh báo.
  (Nguồn: `docs/VIOLET_HARDWARE.md` §3.2 + `diag` census; `boots=476` không có
  crash record.)
- **Không có driver watchdog** trong kernel stock (`/sys/class/watchdog` rỗng).
  `platform/violet/watchdog.c` vì thế là no-op và tự log lý do.
- **QRTR name service** của PID1 đã chạy thật: node `0x1`, thấy node modem `0x0`,
  ADSP `0xa`, node `0x5`, service `0x1000/1001/1002/190/301/42/2b/18/33/f/40/e`.
  Service WLFW `0x45` chỉ có *lookup*, không có server → đúng blocker Wi-Fi.
- **Camera**: 17 subdev, session `0x00530200` (lần đo 2026-11-21), `CAM_ACQUIRE_DEV`
  ISP handle `0x00aa0100`; sensor/ICP trả `errno 22` → chưa capture.

## 5. Câu hỏi mở (chặn Phase 2 — Device Tree)

1. **Chưa có DTS/DTSI của violet trong repo.** `temp/mt689x/*.dts` là của chopin
   (MediaTek), không dùng được. Phase 2 cần lấy `arch/arm64/boot/dts/vendor/qcom/`
   từ OSS `violet-q-oss` (GPL-2.0) rồi đọc từng node.
2. Bảng IRQ/GPIO/regulator/clock cho: DSI panel, touch (NVT), TAS2563, WCD937x,
   SDHCI, WCN3990 SNOC, DWC3, camera CSIPHY — tất cả đang E0.
3. Loại storage controller: eMMC 5.1 (E1) vs dấu vết `ufs_phy_gdsc` (E3) — cần
   đọc node `sdhc`/`ufshc` trong DTS để chốt.
4. Sensor part number + bus (accel/gyro/prox/light/mag).
5. Trạng thái AVB/anti-rollback của p76/p77 và p68 `devinfo`.
6. Nhánh firmware chính xác theo SKU (India `V12.5.1.0.QFHINXM` vs China
   `V12.5.4.0.QFHCNXM`); máy này `ro.boot.hwc=CN` nhưng chưa đối chiếu đủ.

## 6. Bảng trạng thái tổng hợp (theo thang Phase 9)

| Nhóm | Trạng thái | Ghi chú |
| --- | --- | --- |
| SoC/CPU/interrupt | `NOT_TESTED` (E0) | chưa có kernel QEAF chạy trần trên máy |
| Clock/regulator/power | `BLOCKED` | thiếu DTS + driver model chưa có backend SM6150 |
| Storage (read-only) | `HARDWARE_DETECTED` | `mmcblk0p77` đọc được; chưa có driver SDHCI native |
| Display | `FUNCTIONAL` (qua DRM của kernel stock) | 1080×2340@60, scanout thật; chưa có driver DSI native |
| Touch | `HARDWARE_DETECTED` | evdev NVT 1080×2339; chưa có driver input native |
| Backlight | `FUNCTIONAL` (sysfs) | 85% |
| Wi-Fi WCN3990 | `BLOCKED` | thiếu WLFW `0x45`, MAC/calibration p74 |
| Bluetooth | `BLOCKED` | thiếu HCI transport + firmware path native |
| Audio | `BLOCKED` | codec WCD937x defer probe, amp chưa cấp điện, 0 soundcard |
| Camera | `HARDWARE_DETECTED` | 17 subdev, ISP acquire OK, chưa có frame |
| Sensors | `NOT_TESTED` | chưa có node/ID |
| USB | `FUNCTIONAL` (dwc3 + f_mass_storage) | LUN 32 MiB |
| Power/battery | `BLOCKED` | không có fuel gauge/charger native; RTC chỉ set được giờ |
| GNSS | `BLOCKED` | phụ thuộc modem + service layer |
| Fingerprint | `UNSUPPORTED` | không có node |
