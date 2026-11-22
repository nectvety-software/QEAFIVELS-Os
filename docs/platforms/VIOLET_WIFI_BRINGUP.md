# Violet (Qualcomm SM6150) — Wi-Fi host-service bring-up

Trạng thái: **chưa có `wlan0`**. Modem boot thật, nhưng dịch vụ WLFW (QMI `0x45`)
không đăng ký nên firmware WLAN chưa sẵn sàng. Không có dòng nào báo "đã kết
nối" khi chưa có bằng chứng.

## Các thành phần thật được tích hợp

Ramdisk của `dist/boot-vqeaf-violet.img` chứa 4 daemon tĩnh `aarch64-linux-musl`,
được build từ nguồn vendored trong `platform/violet/hosttools/src/` (mỗi dự án
giữ LICENSE riêng):

| Daemon | Nguồn | Vai trò |
| --- | --- | --- |
| `qrtr-ns` (có sẵn trong PID1) | `platform/violet/qrtr_ns.c` | name service QRTR, cần cho kernel 4.14 |
| `pd-mapper` | linux-msm/pd-mapper (vá fallback đọc `*.jsn`) | SERVREG: ánh xạ `wlan_pd`, `tms/pdr_enabled` |
| `tqftpserv` | linux-msm/tqftpserv | phục vụ `wlanmdsp.mbn` cho modem qua QRTR |
| `rmtfs -o /tmp/rmtfs -r` | linux-msm/rmtfs (shim udev bằng sysfs) | EFS modem trong RAM, **không ghi partition** |
| `wlfwd` | ScYurii/wlfwd | handshake WLFW: IND_REGISTER → CAP → BDF → CAL_REPORT → MAC |
| `pd-listen` / `pdr-restart` | ScYurii/wlfwd | userspace `pdr_add_lookup()` / servreg restart cho WLAN PD |

Build: `python scripts/build_violet_hosttools.py`, sau đó `python scripts/package_violet.py`.

PID1 (`platform/violet/host_services.c`) khởi động theo thứ tự:
`qrtr-ns` → `pd-mapper`, `tqftpserv`, `rmtfs` → `/dev/ipa` = 1, boot ADSP/CDSP
→ (khi `wifi up`) mở `/dev/subsys_modem` → `wlfwd` → thử `echo ON > /dev/wlan`.

Các file PD map kiểu QCS615 (`msm/modem/wlan_pd` instance 180, `tms/servreg`,
`wlan/fw`, `kernel/elf_loader`, và `msm/modem/root_pd` với `tms/pdr_enabled`,
`gps/gps_service`) được ghi vào `/tmp/qeaf-pd/` lúc chạy; không bịa firmware.

## Verb QEAFRESCUE để kiểm chứng (chỉ đọc)

```
wifi host          trạng thái từng daemon (pid / exit status)
wifi log           stdout/stderr gần nhất của 4 daemon (/tmp/hostsvc-*.log)
wifi find NAME     tìm board file trong persist/vendor/firmware
wifi ls DIR        liệt kê thư mục
wifi link PATH     readlink
wifi cat FILE      đọc file nhỏ
wifi up            bật modem (opt-in, vì không có vendor service layer)
```

## Bằng chứng đo được trên thiết bị `ca1af25d`

- Sau `wifi up`: `subsys-pil-tz 4080000.qcom,mss: modem: Power/Clock ready
  interrupt received`, `ssctl_new_server: Connection established ... modem's
  SSCTL service` → modem ONLINE thật.
- `hostsvc pd-mapper/tqftpserv/rmtfs` đều `running`; `wlfwd` chạy và ghi
  `waiting for WLFW service 0x45`.
- QRTR-NS thấy các service mới từ ADSP/CDSP (0x42, 0x2b, 0xf, 0x33, 0x18),
  nhưng không có `0x45`.
- `/dev/wlan` là misc class `wlan` major 500:0; các lần thử `ON` chưa thành công.
- `wlan0` absent; UI vẫn báo Wi-Fi không hỗ trợ.

## Các phương pháp đã thử nghiệm (2026-11-19)

| Phương pháp | Cách thử | Kết quả đo được |
| --- | --- | --- |
| PD map đúng kiểu QCS615 | Ghi 2 JSON vào `/tmp/qeaf-pd/`, pd-mapper fallback đọc | `loaded 24 domain/service entries` — có `wlan/fw`, `tms/pdr_enabled`, `mpss_wlan_pd` |
| `pdr-restart` qua QMI 0x42 | Gửi `SERVREG_RESTART_PD` (0x24) | `result 1 error 9` — bị từ chối |
| `pd-listen` (userspace `pdr_add_lookup`) | Đăng ký listener `msm/modem/wlan_pd` qua QMI 0x20 | node `0x0:0x3`: `result 0 error 0`; node khác `error 45`. WLFW vẫn không lên |
| `/dev/wlan` ON | `echo ON > /dev/wlan` (major 500:0), có retry và verb `wifi on` | chưa có FW_READY, chưa báo thành công |
| MAC per-unit | Tìm `wlan_mac.bin` trên mọi partition ext4 + symlink vendor | không có; dùng MAC derived từ serial và ghi rõ |
| **`cnss-daemon` vendor** | `wifi vendor`: mount mọi partition ext4/f2fs tìm `/system/bin/linker64`, rồi exec `/vendor/bin/cnss-daemon` | **không có bionic linker trên partition nào**; exec trả `No such file or directory` (PT_INTERP thiếu) |
| Restart modem sau khi stack sẵn | `wifi off` → `wifi up` với pd-mapper/pd-listen đã chạy | WLFW 0x45 vẫn không đăng ký |
| `wpa_supplicant` vendor | Có `/vendor/bin/hw/wpa_supplicant` | không chạy được (cùng bionic), và cũng chưa có `wlan0` để điều khiển |

Kết luận: phần userspace mà QEAF có thể cung cấp đã đầy đủ và có log chứng minh.
Chặn còn lại nằm ở firmware/modem: không khởi động `wlan_pd`, không yêu cầu
`wlanmdsp.mbn` qua tqftpserv, không publish WLFW 0x45. Hai hướng còn lại cần
nguồn ngoài QEAF: (a) `cnss-daemon` kèm bionic linker/system partition thật, hoặc
(b) kernel mainline có `CONFIG_QCOM_PD_MAPPER` + ath10k như cấu hình đã chứng
minh trong NectvetyOS research.

## Thử nghiệm hướng 1 & 2 (2026-11-19)

**Hướng 1 — dump system / `cnss-daemon` vendor:** có dump
`D:\QeafivelsOfficial\QeafdropOs\out\reference\stock-violet\system.img` (3,75 GB)
và `vendor.img`, đã trích được `cnss-daemon` + phần lớn thư viện QMI. Tuy nhiên
`/system/bin/linker64` là symlink tới `/apex/com.qeafivels.runtime/bin/linker64`,
mà `/apex` trong dump **rỗng** (APEX payload không được dump) → không có linker
lẫn `libc.so`/`libm.so`/`libdl.so` thật để chạy binary bionic. Hướng này bị chặn
bởi dump không đầy đủ, không phải lỗi QEAF.

**Hướng 2 — kernel mainline 7.x:** đóng gói `Image` 7.2.9 của NectvetyOS + DTB
`violet-sm6150` đã inject `qcom,msm-id 355 0`, `qcom,board-id 34 0` với ramdisk
QEAF, rồi `fastboot boot` (RAM only, **không ghi partition**). Kết quả:
- kernel 7.2.9 boot được, QEAFRESCUE trả lời (`/proc/version` xác nhận),
- nhưng `socket(AF_QIPCRTR)` → `Address family not supported`: Image này **không có QRTR**,
- `/sys/class/drm` chỉ có `version`, không có `card0` → `ui=0, drm=0`,
- `/sys/class/remoteproc` rỗng, `ath10k` không có.

Ảnh `dist/boot-mainline-qeaf-test.img` chỉ dùng để thử RAM. Thiết bị đã reboot
lại về p77 (QEAF stock kernel, `ui=1 drm=1`); không có partition nào bị thay đổi.
Image nghiên cứu 7.2.10 có `CONFIG_QRTR_SMD=y`/ath10k không có trong máy — cần
build kernel từ source `D:\os-system\NectvetyOS\kernel\src\linux-7.3.4` với config
và patch trong `_research/violet-sm6150-mainline/` nếu tiếp tục hướng này.

### Build kernel research thật (2026-11-19, tiếp theo)

- Nguồn dùng là `/root/kbuild/linux-7.2.9` trong WSL (nguồn D: bị thiếu file),
  apply 11/19 patch research, config `config-7.2.10-sm6150-2026-10-27`.
- Buộc built-in: `QRTR_SMD=y`, `ATH10K_SNOC=y`, `QCOM_PD_MAPPER=y`,
  `QCOM_Q6V5_PAS=y`, `QCOM_SYSMON=y`, `RPMSG_QCOM_GLINK_SMEM=y`.
  `Image` 36.515.848 B + `violet-sm6150.dtb`; DTB đã inject msm-id 355/board-id 34.
- `fastboot boot` (RAM only) với ramdisk QEAF: kernel `7.2.9-sm6150` boot được,
  `ath10k_snoc` probe (`18800000.wifi`), QRTR socket chạy (in-kernel name service
  chiếm control port), **remoteproc1 modem lên `running`** sau khi QEAF tạo 89
  alias firmware `qcom/qcs615/`.
- Tuy nhiên WLFW 0x45 vẫn không xuất hiện, `wlan0` absent; `remoteproc0 adsp`
  vẫn offline, `rmtfs` exit 1, `/dev/wlan` không tồn tại trên mainline. Modem
  sau đó báo `fatal error without message`. Chưa đạt kết nối.

Thiết bị đã reboot về p77 (QEAF stock kernel, `ui=1 drm=1`); mọi thử nghiệm
mainline đều dùng `fastboot boot`, **không ghi partition**.

### Vòng thử cuối (đã tích lũy vào ramdisk)

- Patch research "FAIL" thực chất **đã có trong source 7.2.9** (`patch --forward`
  báo reversed) — hướng apply tay không cần thiết.
- Đã sửa(mainline): alias firmware `qcom/qcs615/` cho **mọi** file (thêm `adsp.b*`
  → ADSP remoteproc0 lên `running`), tạo `/dev/qcom_rmtfs_mem1` từ
  `/sys/class/rmtfs` và reload rmtfs → `rmtfs: running`, EFS `modem_fs1` trả lời,
  modem ổn định hơn; rebind `ath10k_snoc` sau khi modem up; dịch vụ PDR listener.
- Kết quả vẫn: `wlfwd` `waiting for WLFW service 0x45`, `wlan0` absent.
  `qrtr-lookup` client xác nhận không có server 0x45. Chặn cuối cùng là firmware/
  board data per-unit (persist) + PD/WLFW phía modem, không phải lớp userspace
  của QEAF.

## Tham khảo device tree crDroid (2026-11-19)

Theo `upstream Violet device-tree device.mk (14.0; repository identifier intentionally omitted)`, crDroid
chép **`wifi/WCNSS_qcom_cfg.ini` thật từ device tree** tới
`/vendor/etc/wifi/WCNSS_qcom_cfg.ini`. Đã tải file đó (sha256
`de1961faac00fc6c004aea21c82f731bd2905c8db3e6026a8d1638d080acb3c9`) và stage
vào ramdisk tại cả `/vendor/etc/wifi/` và
`/lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini`, đồng thời không để
`link_firmware_tree()` đè thư mục `wlan` bằng symlink hỏng nữa.

Kết quả đo: file cấu hình đọc được thật (`# This file allows user to override
the factory...`), nhưng **WLFW 0x45 vẫn không lên**, `wlan0` absent trên cả
kernel stock lẫn mainline. Repo crDroid **không có `wlan_mac.bin`** (đúng vì MAC
là dữ liệu per-unit nằm ở persist), nên chặn cuối vẫn là MAC/calibration theo
từng máy — flash ROM gốc không khôi phục được persist.

## Chặn lại ở đâu / cần gì để tiếp

1. persist trên máy không có `wlan_mac.bin` và `WCNSS_qcom_cfg.ini`
   (symlink `/vendor/firmware/wlan/qca_cld/wlan_mac.bin → /mnt/vendor/persist/wlan_mac.bin`
   trỏ tới file không tồn tại). Không có MAC factory; QEAF dùng MAC **derived từ
   serial** và ghi rõ trong report.
2. `bdwlan.bin`/`regdb.bin` chỉ có bản trong modem partition, thiếu bản per-unit
   đã hiệu chuẩn của persist.
3. PD map tự publish chưa đủ: dù `pd-mapper` nạp 24 entry và `pd-listen`
   đăng ký listener cho `msm/modem/wlan_pd` thành công (`result 0 error 0`),
   modem vẫn không khởi động wlan_pd — tqftpserv không nhận request
   `wlanmdsp.mbn`, WLFW 0x45 không xuất hiện. Chặn này nằm ở firmware/PD owner,
   không còn ở lớp service userspace.
4. Sau ~40–60 s modem vẫn reset (IPA/EFS chưa đầy đủ). Đây là lý do QEAF giữ
   modem ở chế độ opt-in (`wifi up`) và không tự bật lúc boot.

Không có bước nào giả tạo trạng thái Wi-Fi. Khi có dump persist/firmware thật
của chính máy, thay thế các file tạm trong `/tmp/wlfw/` và `/tmp/qeaf-pd/` rồi
chạy lại `wifi up`; chỉ báo `wlan0` khi `/sys/class/net/wlan0` tồn tại thật.


---

## Phân tích log modem reset & đường dẫn cnss-daemon (2026-11-20)

Thiết bị `ca1af25d`, kernel **stock p77**, ảnh boot đang chạy với ramdisk đã
stage firmware WCN3990 20.10.30. Các lệnh dùng: `wifi up`, `wifi log`,
`wifi dmesg`, `wifi svc`, `wifi fw`, `wifi find`, `wifi ls`, `pstore`.

### 1. Nguyên nhân modem reset sau ~40–60 s — tách log

Sample `wifi dmesg` mỗi 3 s trong lúc boot modem (`temp/kmsg_reset_capture.txt`,
`temp/capture_modem_reset.py`). Dòng kernel cuối cùng trước khi mất QEAFRESCUE
(t+42 s và t+36 s ở hai lần lặp) **không phải lỗi modem**:

```
qrtr: Modem QMI Readiness RX cmd:0x2 node[0x0]
sysmon-qmi: ssctl_new_server: Connection established ... modem's SSCTL service
firmware tas2563_uCDSP.bin: _request_firmware_load: firmware state wait timeout: rc = -110
tas2563 3-004c: tas2563_uCDSP.bin firmware is not loaded.
<QEAFRESCUE biến mất>
```

Modem thực chất **lên online thật** (`Brought out of reset`, `Power/Clock ready
interrupt`, `Modem QMI Readiness cmd:0x2`, `ssctl_new_server` cho modem). Reset
xảy ra ~30–40 s SAU khi modem online, trùng khớp cửa sổ watchdog 30 s của QEAF.

Log daemon ngay trước reset (`wifi log`, `temp/modem_reset_capture.txt`):

| Daemon | Log cuối | Ý nghĩa |
| --- | --- | --- |
| `pd-mapper` | `loaded 18 domain/service entries` | chạy, fallback 12 PD json (remoteproc class absent) |
| `tqftpserv` | (không output) | chạy, **không nhận request `wlanmdsp.mbn`** |
| `rmtfs` | `failed to open /dev/qcom_rmtfs_mem1` → `uio1` → `/dev/mem` → `exited status=1` | **EFS không có shared memory** |
| `wlfwd` | (chưa start) | chỉ start khi `wifi on` |
| `pdr-restart` / `pd-listen` | (không output) | — |
| `cnss-daemon` | `Binder driver '/dev/binder' could not be opened` → `SIGABRT` | **chết trước khi chạm QRTR** |
| `qlookup` | `lookup 0x45/0x1/0x0 sent` → `recvfrom(): Resource temporarily unavailable` lặp | **không có server 0x45** |

**Kết luận nguyên nhân.reset:** chuỗi là `cnss-daemon abort do thiếu /dev/binder`
→ không ai đăng ký WLFW `0x45` → `wlan_pd` không bao giờ được khởi động →
`tqftpserv` không được yêu cầu firmware → modem không có WLAN/EFS hoàn chỉnh →
reset _(kèm rmtfs exit 1 vì thiếu `/dev/qcom_rmtfs_mem1`, làm EFS càng thiếu)_.
Dòng `tas2563_uCDSP` là firmware codec âm thanh, **không phải** nguyên nhân;
nó chỉ là dòng kernel cuối cùng kịp ghi trước reset.

### 2. Đường dẫn firmware WCN3990 & trạng thái driver

Firmware thật CÓ TRONG ảnh (`wifi fw`, `wifi find`, đối chiếu
`build/violet/stage/lib/firmware/`):

- `/lib/firmware/wlanmdsp.mbn` (3.694.736 B, SHA-256
  `4C3D277812FE0496EE7F486BE80B4DC530EBC2655EB18E1302B642857008D4B8`)
- `/lib/firmware/bdwlan.bin` + 28 biến thể `bdwlan.102 … bdwlan.b38`
- `/lib/firmware/regdb.bin`; `/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini`
  (sha256 `de1961fa…c3c9`) + `wlan_mac.bin`

**Không có** file `qwlan*`/`otp*` riêng — WCN3990 dùng `bdwlan.*` + `wlanmdsp.mbn`
là đúng bộ; không bịa thêm.

Trạng thái driver/đăng ký (`wifi mod`, `wifi svc`, `wifi net`):

- `/sys/module`: `bluetooth`, `wlan`, `cfg80211` — module driver có mặt,
  nhưng `/sys/class/net` **không có `wlan0`**.
- `wlan_hdd_state wlan major(500) initialized` trong dmesg — driver đã probe.
- `wifi bring: stage=1 vendor=1 modem=1 ... copied=0 files=0 wlan=0` — bringup
  chưa copy file vì chưa có yêu cầu từ service.
- QRTR-NS: `lookups` có `0x45/0x0 → 0x4023` nhưng bảng `servers` **không có
  entry `0x45`**; `qlookup` nhận `recvfrom timeout`. Driver không báo "thiếu file
  firmware" cụ thể — nó đang **chờ phản hồi QMI/WLFW `0x45`** (IND_REGISTER /
  FW_READY), tức chờ cnss-daemon, không chờ file.

### 3. cnss-daemon — dump có đủ hay cần Fastboot ROM?

**Dump hiện tại ĐỦ thư viện/APEX để chạy cnss-daemon; không cần trích thêm từ
Fastboot ROM.** Bằng chứng:

- `wifi ls /apex`: `com.qeafivels.runtime` (không còn rỗng như ghi chú cũ ngày
  19/11). Bên trong có `bin/{linker,linker64,linkerconfig}`,
  `lib64/bionic/{libc.so,libm.so,libdl.so,...}` — đúng PT_INTERP Bionic.
- `/vendor/lib64` có **QMI/vendor libs** cnss-daemon cần:
  `libqmi_cci.so`, `libqmi_common_so.so`, `libcld80211.so`,
  `libperipheral_client.so`, `libmdmdetect.so`, `libqmiservices.so`,
  `libbinderdebug.so`, ... (`readelf -d cnss-daemon` liệt NEEDED).
- cnss-daemon **đã thực thi tới dòng Binder** (`Binder driver '/dev/binder'
  could not be opened`) — tức linker đã resolve mọi NEEDED thành công. Nếu
  thiếu lib/APEX, nó đã chết ở `PT_INTERP`/linker từ sớm như ghi chú cũ.

`libcutils.so`/`liblog.so`/`libnl.so` `wifi find` trả "not found" vì hàm tìm
chỉ quét `/vendor/lib64`, `/lib/firmware`, ... **không quét APEX** — chúng nằm
trong `/apex/com.qeafivels.runtime` (đã được linker nạp). Đây không phải blocker.

**Blocker duy nhất còn lại là binder node:**

- `/sys/module/binder` tồn tại, `parameters/devices = binder,hwbinder,vndbinder`
  (module binder có trong kernel stock).
- Nhưng `/proc/devices` (character devices) và `/proc/misc` **không có dòng
  `binder`**, `/sys/class/misc` không có `binder`, `/dev/binder` không tồn tại.
- `ensure_binder_node()` trong `host_services.c` chỉ dò chữ `"binder"` trong
  `/proc/devices`; không thấy → `mknod` không bao giờ chạy → `wifi vendor` vẫn
  `SIGABRT` dù gọi lại nhiều lần.

### Phương án khả thi nhất tiếp theo (không mock, không ghi partition)

Sửa `ensure_binder_node()` để tạo `/dev/binder` từ **minor thật** thay vìMajor
trong `/proc/devices`: kernel stock binder đăng ký qua `misc` (major 10) hoặc là
char device 499:0 nhưng không xuất hiện trong `/proc/devices` do config. Cách
làm an toàn, có kiểm chứng: thử `mknod /dev/binder` lần lượt với cặp
`10:<minor lấy từ /proc/misc nếu có>` rồi `499:0`, `499:1`…; giữ cặp nào
`open(O_RDWR)` thành công (cùng cơ chế probe hiện tại nhưng mở rộng danh sách
major/minor). Sau đó gọi lại `wifi vendor` + `wifi on` và kiểm tra:
`qrtr-ns servers` phải xuất hiện `0x45`, `qlookup 0x45` trả node/port thật,
`tqftpserv` nhận request `wlanmdsp.mbn`, rồi mới expect `wlan0`.

Vẫn không đảm bảo thành công: ngay cả khi cnss-daemon chạy, các chặn gốc còn lại
(persist per-unit `wlan_mac.bin` / BDF hiệu chuẩn, `rmtfs` cần shared memory,
modem reset) chưa được giải quyết. Sẽ chỉ báo `wlan0` khi
`/sys/class/net/wlan0` tồn tại thật — không mock, không bypass QMI.


---

## Trích xuất sysroot Bionic từ Fastboot ROM violet (2026-11-20)

### 1. ROM nguồn & định dạng

- Fastboot ROM gốc có sẵn tại:
  `D:\QeafivelsOfficial\QeafdropOs\out\reference\stock-violet\`
  chứa `system.img` (3.758.096.384 B) và `vendor.img` (2.147.483.648 B) **kèm
  boot/dtbo/cust/vbmeta** (đây là dump phân vùng, không cần giải nén .tgz).
- `file` không có trên host; kiểm tra magic bằng Python: cả hai có `53 ef` tại
  offset 1080 → **raw ext4, KHÔNG phải sparse** → không cần `simg2img`.
- Mount read-only trong WSL Ubuntu-24.04:
  `mount -o loop,ro system.img /mnt/violet_sys` và vendor.img → `/mnt/violet_ven`.

**Đính chính quan trọng:** ghi chú cũ (19/11) nói "APEX trong dump rỗng" là do
mount nhầm nhánh `/apex` (rỗng). APEX thật nằm ở **`/system/apex/`** trong
system.img: `find /mnt/violet_sys/system/apex -maxdepth 1 -type d | wc -l` = **26**,
gồm `com.android.runtime`, `com.android.vndk.current`, `com.android.wifi`, …
`com.android.runtime/bin` có `linker`, `linker64`, `linkerconfig`, `crash_dump64`;
`lib64/bionic` có `libc.so`, `libm.so`, `libdl.so`, `libdl_android.so`.

### 2. Sysroot `/opt/bionic-cnss` (trong WSL) — đã copy

| Nguồn ROM | Đích sysroot |
| --- | --- |
| `system/system/apex/` (26 module) | `/opt/bionic-cnss/apex/` (26 module) |
| `system/apex/com.android.runtime/bin/linker{,64}` | `system/bin/linker{,64}` |
| `system/apex/.../bin/linkerconfig` | `system/bin/linkerconfig` |
| `system/lib/`, `system/lib64/` | `system/lib/`, `system/lib64/` (673 .so) |
| `vendor/lib/`, `vendor/lib64/` | `vendor/lib/`, `vendor/lib64/` (683 .so) |
| `vendor/bin/cnss-daemon` (95.024 B) + QMI tools | `vendor/bin/` |
| `system/linkerconfig/*` | `/opt/bionic-cnss/linkerconfig/` |

Tổng: **1,2 GB, 3.094 file**, apex .so = 624. Artifact đóng gói trong dự án:
`dist/bionic-cnss-sysroot-min.tar.gz` (12 MB, bản minimal 34 MB gồm linker +
13 lib NEEDED + cnss-daemon), `dist/bionic-cnss-sysroot-full-manifest.txt`,
`dist/cnss-daemon-needed.txt`, `dist/cnss-daemon-interp.txt`.

### 3. Đối chiếu phụ thuộc cnss-daemon (binary thật từ ROM)

`readelf -d /opt/bionic-cnss/vendor/bin/cnss-daemon` → 13 NEEDED:
`libcutils.so, liblog.so, libqmi_cci.so, libqmi_common_so.so, libnl.so,
libcld80211.so, libperipheral_client.so, libmdmdetect.so, libqmiservices.so,
libc++.so, libc.so, libm.so, libdl.so`.
PT_INTERP = `/system/bin/linker64` (có trong sysroot).

 Resolve từng lib trong sysroot: **13/13 OK, 0 MISSING** (manifest tại
`dist/cnss-daemon-needed.txt`). Các lib VNDK/QMI lấy từ
`com.android.vndk.current/lib64`, `system/lib64`, `vendor/lib{,64}` của ROM.

### 4. Chạy thử cnss-daemon thật trên violet — kết quả đo được

Trên thiết bị đã có sẵn (từ ramdisk QEAF): `/system/bin/linker64`,
`/apex/com.qeafivels.runtime/bin/{linker,linker64,linkerconfig}`,
`/vendor/bin/cnss-daemon` (binary stock thật). Gọi `wifi vendor` rồi `wifi log`:

```
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
Binder driver '/dev/binder' could not be opened. Terminating: Opening '/dev/binder' failed: No such file or directory
libc: Fatal signal 6 (SIGABRT), code -1 (SI_QUEUE) in tid 540 (cnss-daemon)
```

**Kết luận then chốt:** cnss-daemon stock chạy được tới dòng Binder — linker đã
resolve xong 13 lib (nếu thiếu lib/APEX nó đã chết ở PT_INTERP như ghi chú cũ).
Vậy **trích xuất sysroot ROM không phải là chặn còn lại**; blocker vẫn là
`/dev/binder` (xem mục 3 ở trên: module binder có params nhưng không có device
node trong `/proc/devices`, `/proc/misc`, `/sys/class/misc`). Dù đã stage đủ
toàn bộ APEX + linker + lib từ ROM, `wifi vendor` vẫn SIGABRT y hệt.

Ghi chú về yêu cầu chroot/bind-mount: môi trường QEAFRESCUE là PID 1 đơn tiến
trình, không có tiện ích `chroot`/`mount --bind` trong ramdisk; các mount point
(`/dev`, `/proc`, `/sys`, `/mnt/vendor/persist`) đã được PID 1 gắn sẵn (xem
`/proc/mounts`). Vì blocker là binder node chứ không phải thiếu thư viện, việc
dùng chroot không thay đổi kết quả. Hướng xử lý thực tế vẫn là tạo `/dev/binder`
từ minor thật như đã nêu ở mục 3, sau đó mới expect WLFW `0x45`/`wlan0`.

Thiết bị trong lúc thử có một lần reboot ngắn ( modem reset nền ) nhưng đã về
p77 QEAF stock `ui=1 drm=1`; **không partition nào bị ghi**. Không mock, không
bypass QMI; chưa có `wlan0` nên tiếp tục báo chưa hỗ trợ.


---

## `/dev/binder` từ kernel thật — đo trên máy (2026-11-20, ảnh `4d21958f`)

### 1. Kernel violet có Binder, nhưng là **binderfs**, không phải misc device

Đọc trực tiếp trên thiết bị qua kênh rescue (`wifi cat` / `wifi ls`):

| Nguồn | Kết quả thật |
| --- | --- |
| `/proc/filesystems` | có dòng `nodev	binder` → **binderfs được biên vào kernel** |
| `/proc/devices` | `499 binder` (Character devices) → vùng char động do binderfs cấp |
| `/proc/misc` | **không** có `binder` (chỉ memory_bandwidth, tas2563, msm_*, …) |
| `/sys/class/misc` | **không** có `binder` |
| `/sys/module/binder/parameters/devices` | `binder,hwbinder,vndbinder` (tham số `binder_devices_param`) |

Suy ra: `CONFIG_ANDROID_BINDER_IPC` + `CONFIG_ANDROID_BINDERFS` đã bật sẵn trong
kernel stock; **không cần build lại kernel/boot image cho Binder**. Major 499 là
`alloc_chrdev_region(..., "binder")` của binderfs, nhưng node chỉ được binderfs
tạo khi **mount**; vì vậy `mknod 499:0` luôn `ENXIO` (đúng như log cũ). Đây là lý
do `ensure_binder_node()` cũ (chỉ dò `/proc/devices` rồi mknod) không bao giờ
tạo được node.

### 2. Sửa trong `platform/violet/host_services.c`

- `binderfs_supported()`: dò `binder` trong `/proc/filesystems`.
- `ensure_binderfs_nodes()`: `mkdir /dev/binderfs` → `mount("binder",
  "/dev/binderfs", "binder", 0, NULL)` (EBUSY = đã mount) → symlink
  `/dev/binder`, `/dev/hwbinder`, `/dev/vndbinder` → `/dev/binderfs/<name>`,
  `chmod 0666` node, log major:minor thật.
- `ensure_binder_node()`: nếu `/dev/binder` đã `open(O_RDWR)` được thì thôi;
  nếu không thì binderfs trước, chỉ khi kernel không có binderfs mới rơi về
  probe char-device cũ (fail-closed).
- `binder_node_present()`: kiểm tra `open()` thật thay vì so major.
- Không cần bind-mount vào "sysroot của cnss-daemon": `launch()` chỉ `fork()` +
  `exec` (setsid, redirect log), **không chroot** — cnss-daemon nhìn đúng `/dev`
  của PID 1.

### 3. Kết quả đo được trên máy

```
/proc/mounts: binder /dev/binderfs binder rw,relatime,max=1048576 0 0
/dev: vndbinder hwbinder binder binderfs wlan subsys_modem ...
/dev/binder -> /dev/binderfs/binder
hostsvc binder: present
hostsvc cnss-daemon: running pid=542      (trước đây: exited status=-6)
--- cnss-daemon ---
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```

Không còn dòng `Binder driver '/dev/binder' could not be opened` và không còn
`Fatal signal 6 (SIGABRT)`. Blocker `/dev/binder` đã hết.

### 4. WLFW `0x45` / `wlan0`: vẫn chưa lên — nguyên nhân đo được

- `qrtr-ns servers` (đầy đủ) **không có `0x45`**:
  `0x1001/0x0 0x34/0x1 0x1001/0x1 0x41d/0x0 0x1000/0x0 0x40/0x1 0x42/0x4c@0xa
  0x2b/0x17@0xa 0xf/0x40@0xa 0x33/0xd@0xa 0x18/0x43@0xa 0x2b/0x14@0x5
  0xf/0x20@0x5 0x42/0x4a@0x5 0x33/0x5@0x5 0x18/0x1@0x5 ... 0x301/0x0@0x5`
- `/tmp/hostsvc-qlookup.log`: `lookup 0x45/0x1/0x0 sent` rồi lặp
  `recvfrom(): Resource temporarily unavailable` → modem không trả lời.
- Modem **đã boot thật**: `wifi modem` → `subsys1 name=modem state=ONLINE`,
  `wifi modem: fd=11 rc=1 thread_done=1`; dmesg `modem: Brought out of reset`,
  `Modem QMI Readiness RX cmd:0x2`.
- `/dev/wlan` là `wlan_hdd_state` (major 500, tên trong `/proc/devices` là
  `qcwlanstate`). Handler làm `copy_from_user(buf, user_buf, 3)` vô điều kiện,
  nên phải ghi **đúng 3 byte**. `host_services.c` giờ thử lần lượt `ON `,
  `ON
`, `ON` và log từng lần.
- Kernel ring ghi nhận chuỗi thật:
  ```
  Wifi Turning On from UI
  wlan: Loading driver v5.2.022.5U
  ipa ipa3_uc_reg_rdyCB:3134 bad parm
  Timed-out!!            (≈20 s sau, lặp lại mỗi lần ghi)
  ```
  `wlan_hdd_state_ctrl_param_write()` trả `-EINVAL` sau timeout, nên rescue báo
  `wifi on: refused (errno 22)`. Không có dòng `icnss:` nào → driver load dừng
  trước khi icnss/WLFW handshake hoàn tất.
- `wlan0` vẫn không có: `wifi: wlan0 present: no`, `/sys/class/net` chỉ có
  lo/sit0/bond0/dummy0/ip6tnl0/ip_vti0/ip6_vti0.

### 5. Blocker còn lại (không mock, không bypass)

1. Modem không quảng bá WLFW `0x45` sau khi ONLINE → qcacld `Timed-out!!`.
2. `rmtfs` vẫn `exited status=1`: không có `/dev/qcom_rmtfs_mem*`
   (`/sys/class/misc/qcom_rmtfs_mem*` = ENOENT), `/dev/qcom_rmtfs_uio1` và
   `/dev/mem` cũng vắng → không có shared memory cho modem/WLAN calibration.
3. Vì (1)+(2), không có `wlanmdsp` load, không có `wlan0`. Sẽ chỉ tuyên bố
   `wlan0` khi `/sys/class/net/wlan0` tồn tại thật.

Kết luận: hạng mục **/dev/binder đã xử lý dứt điểm ở mức kernel thật** (binderfs
mount + node thật + cnss-daemon chạy), nhưng WLFW `0x45`/`wlan0` **chưa đạt**;
phần còn lại nằm ở modem WLFW + rmtfs shared memory, không phải binder.


---

## GNSS & wpa_supplicant trên violet (2026-11-21)

### Điều tra hiện trạng ( thiết bị `ca1af25d`, kernel stock p77 )

- `/sys/class/gnss`: **absent**; `/dev/gnss*` không có; `/sys/module/gnss` không có;
  không có dòng `gnss/gps` nào trong `wifi dmesg`. Kernel stock **không expose
  GNSS driver/device node**.
- Có sẵn trong `/vendor/bin/hw` (từ ROM thật):
  `android.hardware.gnss@2.1-service-qti`, `wpa_supplicant`,
  `android.hardware.wifi@1.0-service`. Có `/vendor/etc/gps.conf` thật
  (NTP, SUPL, XTRA…) + `gnss_antenna_info.conf`, `izat.conf`, `flp.conf`.
  Firmware GPS TZ `gptest.b00..b07` + `gptest.mdt` trong `/vendor/firmware_mnt/image`.
  Tuy nhiên `/mnt/vendor/persist/rfs/apq/gnss` rỗng.
- `wpa_supplicant`: binary Bionic thật `/vendor/bin/hw/wpa_supplicant`, PT_INTERP
  `/system/bin/linker64`, 15 NEEDED — resolve **15/15 OK** từ sysroot ROM.

### Đã làm (code + build + flash OK)

- `scripts/package_violet.py`: stage thêm **31 thư viện Bionic 64-bit thật** từ
  sysroot ROM (`/opt/bionic-cnss`, trích từ `stock-violet/system.img`) vào
  `/system/lib64` của ramdisk: `libcrypto.so`, `libssl.so`, `libbinder_ndk.so`,
  `libandroid_runtime_lazy.so`, `android.hardware.wifi.supplicant-V1-ndk.so`,
  `libprocessgroup.so`, toàn bộ `android.hardware.gnss@*.so`, `libgps.utils.so`,
  v.v. Đã verify **0 file 32-bit** (lần đầu lấy nhầm `lib/` → `ELF32`, đã sửa).
- `platform/violet/host_services.c/.h`: thêm `vio_host_run_bionic()` — chạy binary
  stock qua **linker64 thật** với `LD_LIBRARY_PATH` đúng và redirect log vào
  `/tmp/hostsvc-*.log`. Không mock, không stub.
- `platform/violet/channel/fs/rescue.c`: thêm verb `wifi sup` (chạy
  `/vendor/bin/hw/wpa_supplicant -Dnl80211 -i wlan0 -c …`) và `wifi gnss`
  (chạy `android.hardware.gnss@2.1-service-qti`).

### Kết quả chạy thật trên máy (bằng chứng log)

```
wifi sup  -> started
wifi gnss -> started
/tmp/hostsvc-wpa_supplicant.log:
  linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
  Binder driver '/dev/binder' could not be opened. Terminating: Opening '/dev/binder' failed: No such file or directory
  libc: Fatal signal 6 (SIGABRT) ...
/tmp/hostsvc-gnss-hal.log:
  Binder driver could not be opened. Terminating.
  libc: Fatal signal 6 (SIGABRT) ...
```

**Kết luận:** sau khi stage đủ thư viện, cả wpa_supplicant lẫn GNSS HAL đều
**link thành công và chạy tới dòng Binder** rồi abort — y hệt cnss-daemon. Vậy
"chưa có supplicant/GNSS" **không phải do thiếu lib/APEX** (đã fix phần đó bằng
file thật từ ROM), mà do **cùng blocker `/dev/binder`** và, với GNSS, thêm việc
kernel không có device node `/dev/gnss`. Không báo "đã có GNSS/supplicant" khi
chưa có bằng chứng chạy tới mức cung cấp dịch vụ.

### Hướng tiếp theo (không mock)

1. Giải quyết `/dev/binder` chung cho cả 3 daemon (xem mục trước) — khi đó
   wpa_supplicant/cnss-daemon mới tới được QRTR/WLFW.
2. wpa_supplicant cần `wlan0` thật (hiện chưa có) — sau WLFW `0x45` lên mới có
   ý nghĩa chạy `wifi sup` với `-i wlan0`.
3. GNSS cần kernel expose GNSS (device `/dev/gnss*`/`/sys/class/gnss`) + modem
   GNSS service; HAL service stock không tự tạo ra device node. Đây là chặn
   kernel/firmware, không sửa được bằng userspace.

Ảnh đã flash: `dist/boot-vqeaf-violet.img` (sha256
`771fc85f7669a647ab19f9baefa3a336c31024c09218b6df37af58910de8fbae`), 31.973.376 B
trên p77. Thiết bị về p77 QEAF stock bình thường; không ghi partition khác.


---

## Bật debug cnss-daemon & trạng thái WLFW (2026-11-21, sau nạp lại OS)

### Tiến triển đã xác nhận trên máy
- `wifi vendor` mount **binderfs** thành công: `/dev/binderfs/{binder,hwbinder,vndbinder}`
  tồn tại, `/dev/binder -> /dev/binderfs/binder`.
- `hostsvc binder: present` (trước đây `absent`), `hostsvc cnss-daemon: running`
  (pid 535/544), `rmtfs: running`. cnss-daemon **không còn SIGABRT do binder**.
- `qrtr-ns` vẫn chỉ có `lookup 0x45` (`0x45/0x0->0x4024`) nhưng **không có
  NEW_SERVER svc=0x45** trong bảng servers; `wifi on` trả EINVAL (errno 22);
  `wlan0` vẫn absent.

### Đã thử bật debug (rebuild + flash 3 lần)
- Flag binary stock (từ `strings cnss-daemon`):
  `-n/--nodaemon`, `-d` (có `-dd for more`), `-l` (log to logcat), `--help`.
- Đổi `kCnssArgs` trong `host_services.c` lần lượt thành
  `-n -d -d`, `-n -d -d -l`, `-n -dd`; build + flash ảnh
  (`sha256 5064f2c3…9b5` là bản cuối) rồi `wifi vendor`.
- Kết quả `/tmp/hostsvc-cnss-daemon.log` **luôn chỉ có 2 dòng linker warning**:
  ```
  linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
  ```
  Không có dòng debug nào từ cnss-daemon dù flag đã bật.

### Nguyên nhân debug mất (bằng chứng)
- cnss-daemon log qua `__android_log_print`/`liblog.so` (strings + NEEDED).
- Kernel violet **không có android logger device**:
  `/dev/log` absent, `/proc/misc` không có dòng `log`, `/sys/module/logger`
  absent, không có `logcat` trong dump. Vì vậy mọi logcat/debug của
  cnss-daemon bị drop dù `-d/-dd` có hiệu lực. Không phải lỗi flag hay
  lỗi launcher (launcher capture stdout/stderr đúng cho các daemon khác).

### Kết luận & hướng
- Chặn binder đã tháo; cnss-daemon sống. Chặn WLFW `0x45` vẫn nằm ở phía
  modem/firmware/service (không publish server), **không phải do thiếu lib
  hay binder**.
- Để thấy debug cnss-daemon cần một trong: (a) kernel có `CONFIG_ANDROID_LOGGER`
  + node `/dev/log/*` + `logcat`, hoặc (b) bản cnss-daemon emit stderr thay vì
  logcat (không có trong dump stock). Cả hai đều cần nguồn ngoài userspace QEAF.
- Không mock/bypass QMI; tiếp tục báo chưa có `wlan0` cho tới khi QRTR có
  `NEW_SERVER svc=0x45` thật.


---

## Hướng "cnss-daemon emit stderr" qua liblog shim (2026-11-21)

Yêu cầu: tạo kernel có `CONFIG_ANDROID_LOGGER`+/dev/log+logcat, HOẶC bản
cnss-daemon emit stderr. Đã điều tra cả hai:

### Hướng kernel — không khả thi với nguồn có sẵn
- Quét toàn D: + WSL: chỉ có nguồn kernel **mainline** `linux-7.2.9`,
  `linux-7.3.4` (`/root/kbuild`, `D:\os-system\NectvetyOS\kernel\src`).
  **Không có source kernel stock 4.14 của violet** (chỉ có boot.img đã compile).
- Mainline 7.x đã xóa `CONFIG_ANDROID_LOGGER`; kernel stock thì không có source
  để build. Vì vậy không build được kernel có android logger từ những gì có.

### Hướng emit stderr — đã làm (liblog shim, build + flash OK)
- `platform/violet/hosttools/src/liblog-shim/liblog_shim.c`:
  thư viện `liblog.so` **freestanding** (`-nostdlib`, không NEEDED), tự viết
  syscall `write(2)` + printf tối giản, export 21 symbol `__android_log_*`
  (kể cả `__android_log_error_write`, `_assert`, version `LIBLOG` + `LIBLOG_M`)
  → in ra stderr dạng `[D tag] ...`.
- Build bằng zig 0.13.0 target `aarch64-linux-musl`, `-shared`, SONAME
  `liblog.so`, version script `liblog.map`.
- `scripts/package_violet.py`: thay `liblog.so` bằng shim ở
  `/system/lib64/` **và** `/apex/com.qeafivels.runtime/lib64/`, bản thật giữ
  lại thành `liblog.real.so`. Cuối cùng bn flash sha
  `eee78fd7b9ef0dc2bc2c1670551499b90aa8cb98a99b57d9e903fa07d1304c83`.
- Quá trình sửa lỗi linker (có bằng chứng từng bước): glibc `libc.so.6` →
  musl `libc.so` → freestanding; thiếu SONAME (verneed) → thêm SONAME;
  thiếu `LIBLOG_M` version node → thêm; thiếu `__android_log_error_write`
  → export đủ 13 symbol libcutils/libutils cần.

### Kết quả đo được (trung thực)
- cnss-daemon **chạy được với shim**: `hostsvc cnss-daemon: running pid=540`,
  `binder: present`, **không còn lỗi linker / SIGABRT**.
- Tuy nhiên `/tmp/hostsvc-cnss-daemon.log` **vẫn chỉ có 2 dòng warning linker**,
  không có dòng debug nào từ cnss-daemon, kể cả sau `wifi up` boot modem.
  Vì vậy **không thu được thông tin "đang chờ gì"** từ log. Có thể binary
  stock không phát logcat/debug trong trạng thái chờ hiện tại, hoặc Bionic
  linker namespace vẫn phân giải liblog từ chỗ khác dù đã thay 2 vị trí.
- QRTR: vẫn chỉ `lookup 0x45`, không có `NEW_SERVER svc=0x45`; `wlan0` absent.

### Kết luận
- Đã thực hiện đúng yêu cầu "emit stderr" bằng shim thật (không mock QMI),
  nhưng **không mang lại debug hữu ích** — cnss-daemon vẫn im lặng. Hướng
  kernel thì thiếu source stock. Không có thêm việc userspace nào có thể làm
  để buộc binary stock phát log nếu chính nó không gọi hàm log.
- Trạng thái Wi-Fi không đổi: binder OK, cnss chạy, WLFW `0x45` chưa đăng ký,
  chưa có `wlan0`. Tiếp tục báo chưa hỗ trợ; không giả tạo.


---

## Xác nhận bằng REA & đánh giá "port driver từ ROM" (2026-11-21)

### Câu hỏi
Dùng REA (morluto/rea) hoặc porting trực tiếp để "port driver wifi/bluetooth/
camera/vân tay + các driver thiếu" từ ROM Android gốc sang QEAF.

### Thiết lập REA
- `npx rea-agents@6.5.0 doctor --client opencode`: ban đầu `config_drift`,
  đã setup (`setup --client opencode --yes`) → registration vào
  `C:\Users\doxuanhop\.config\opencode\opencode.jsonc`
  (backup `.rea.backup`), tool REA (207) xuất hiện trong session.
- Ghidra 12.1.4 có sẵn tại `D:\MRE\tools\ghidra\ghidra_12.1.4_PUBLIC`
  (doctor: `ghidra available:true`, native_decompiler OK, JDK 21 OK). Cần
  restart OpenCode để MCP kế thừa `GHIDRA_INSTALL_DIR` thì provider decompile
  mới active trong session hiện tại.
- REA `open_binary` trên `build/violet/hosttools/cnss-daemon` → ELF arm64,
  sha256 `ff97ef004546d65d0192d031c566c7a1e4c0ab0773698918665539354ed46256`,
  hash verified (Evidence `ev_8c3b43c3...`). `inspect_adb_client` → ADB 37.0.1.

### Phát hiện quyết định: KHÔNG CÓ GÌ ĐỂ "PORT" Ở MỨC KERNEL DRIVER
- `/proc/version` trên máy: `Linux version 4.14.215-azure-Pixel+ ... clang
  r450784d ... 2024` — chính là **kernel stock của ROM violet**.
- Kernel dạng **monolithic**: `/lib/modules` absent, `/proc/modules` unreadable
  → mọi driver đều **built-in trong kernel đang chạy**.
- `/sys/module` có `wlan`, `cfg80211`, `bluetooth`; `/dev` có `wlan` (major 500),
  `input`, `qcom_rmtfs_uio1` — driver wifi/bt/touch/camera/fingerprint đã có
  sẵn từ kernel stock. QEAF boot chính kernel này (flash p77 boot.img).
- Toàn bộ HAL `.so` (wifi/bt/camera/fingerprint/GNSS) đã trích từ `vendor.img`
  vào `/opt/bionic-cnss` (13/13 + 15/15 NEEDED, 0 missing).

### Kết luận
"Port driver từ ROM" là hướng sai vì driver kernel **đã current chạy sẵn**;
không thể và không cần biên dịch lại driver nhị phân từ boot.img sang kernel
khác (cùng kernel stock thì đã có). Chặn thực sự vẫn là **userspace/service/
firmware**: WLFW `0x45` do modem publish, cnss-daemon chạy tới binder rồi im
lặng, `wlan0` absent. REA decompile HAL (khi active Ghidra) chỉ xác nhận bản
chất HAL, không tạo ra service `0x45` hay sửa được EFS/modem.

Hướng có ý nghĩa duy nhất vẫn là giải quyết WLFW/EFS (đã nêu ở mục trước),
không phải port driver. Không báo "đã port/hỗ trợ driver" khi driver đã có
sẵn và dịch vụ vẫn chưa lên.


---

## Kiểm tra binder / cnss-daemon / WLFW lần cuối (2026-11-21)

### 1. Kernel binder — KHONG can doi defconfig / build kernel
- `wifi cat /proc/filesystems` → `nodev	binder` (binderfs co san).
- `wifi cat /proc/misc` → **khong co dong binder** => day la binderfs, KHONG
  phai misc device. Khong ap dung mknod theo /sys/class/misc/binder/dev.
- Vi `/proc/filesystems` da co binder, KHONG can bat
  `CONFIG_ANDROID_BINDER_IPC=y` / `CONFIG_ANDROID_BINDERFS=y` roi build lai
  boot image. Nguon kernel 4.14 stock cung khong co trong repo de build.

### 2. /dev/binder thật
- `wifi vendor` mount binderfs: `/dev/binderfs/{binder,hwbinder,vndbinder,
  binder-control}` ton tai; symlink `/dev/binder -> /dev/binderfs/binder`,
  `/dev/hwbinder`, `/dev/vndbinder` deu co.
- `wifi svc`: `hostsvc binder: present`, `hostsvc cnss-daemon: running pid=541`.
- Bằng chứng trực tiếp: `/proc/541/fd/15 -> /dev/binderfs/vndbinder` —
  cnss-daemon dang giu binder node that. Khong can chroot: PID1 chay daemon
  voi `/dev` da mount san; sysroot `/opt/bionic-cnss` chi cung cap .so qua
  linker64/LD_LIBRARY_PATH.

### 3. Log cnss-daemon chay that (-n -dd)
`/tmp/hostsvc-cnss-daemon.log` (toan bo):
```
linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
WARNING: linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"
```
- Khong con "Binder driver could not be opened", khong SIGABRT, khong
  "missing hwservicemanager/servicemanager". Da kiem tra them:
  khong co `hwservicemanager`/`servicemanager` trong sysroot, nhung cnss-daemon
  **khong abort** (property_get tra gia tri mac dinh). Debug van khong xuat hien
  vi liblog/Android logcat khong co device tren kernel stock.

### 4. WLFW 0x45 / wlan0
- `wifi qlookup` (qrtr-lookup 0x45)无论是:
```
lookup 0x45/0x1/0x0 sent
ERROR libqrtr: recvfrom(): Resource temporarily unavailable  (x11)
```
- `qrtr-ns servers`: co cac service 0x34, 0x1001, 0x41d, 0x1000, 0x40, 0xe,
  0x42, 0x2b, 0xf, 0x33, 0x18 ... **KHONG CO svc=0x45**.
- `wifi net`: `lo, dummy0, bond0, sit0, ip6tnl0, ip_vti0, ip6_vti0,
  bonding_masters` — **KHONG CO wlan0**.
- Ket luan: binder da OK, cnss-daemon song, nhung WLFW 0x45 van khong duoc
  modem/firmware publish. Khong mock/bypass QMI; khong bao "da ket noi".


---

## RMTFS UIO + SERVREG WLAN-PD watcher implementation (2026-11-21)

### Source changes

- `platform/violet/hosttools/src/rmtfs/sharedmem.c`: after reading the real
  UIO `maps/map0/addr` and `maps/map0/size`, reject zero/overflowing or
  unrepresentable ranges, mmap the real map0 at offset zero, and emit
  `RMTFS UIO ready: client=1 addr=... size=... mmap=...` only after mmap
  succeeds.
- `platform/violet/host_services.c`: UIO publisher now also rejects size
  overflow before creating `/dev/qcom_rmtfs_uio1`. It still requires sysfs
  device name exactly `rmtfs`, reads its kernel-provided dev number/map
  metadata, and does not invent a physical address.
- RMTFS remains `-P -r -v`: raw modemst1/modemst2/fsc/fsg are exposed through
  actual PARTNAME links and read-only shadow storage. Writes requested by the
  modem remain volatile in RAM; this is deliberately not a persistent EFS
  repair.
- `platform/violet/hosttools/src/wlfwd/pd-listen.c`: added `--watch` mode.
  It performs QRTR lookup for SERVREG 0x42 (version 0 = any), waits for actual
  NEW_SERVER/DEL_SERVER control events, and sends QMI
  SERVREG_REGISTER_LISTENER_REQ (0x20) to each discovered endpoint. It checks
  the matching response's transaction/message ID and result/error fields.
- PID1 starts this watcher after pd-mapper. It does not send
  SERVREG_RESTART_PD, publish a fake QRTR server, or synthesize WLFW 0x45.

### Host verification

- `python scripts/build_violet_hosttools.py`: PASS; all 7 hosttools compiled,
  including RMTFS and the QRTR-linked pd-listen watcher.
- `python scripts/build_violet.py --clean`: PASS; vqeaf-init 11,144,376 bytes,
  269 objects.
- `python -m unittest tests.test_qeapp tests.test_qeapp_native
  tests.test_violet_wifi_bringup -v`: PASS, 71 tests.
- `python scripts/package_violet.py --skip-build`: PASS; initramfs and boot
  image validation plus rescue-channel audit passed.
- Image: `dist/boot-vqeaf-violet.img`, 32,108,544 bytes,
  SHA-256
  `9e1367498117cfe93feca2e49275d1d2d9a9804561038ea7568bb4350db42315`.

### Not yet verified on device

At build time `adb devices -l` returned no attached device. The new RMTFS
success line, UIO mmap, SERVREG watcher events, QMI 0x20 response, WLFW 0x45,
and wlan0 therefore remain unverified on the phone. Do not infer modem or Wi-Fi
success from host build/package tests. No flash or partition write was made.

### Required on-device acceptance

1. Boot the image, run `wifi host` and `wifi log`; require
   `RMTFS UIO ready` and `rmtfs: running`.
2. Confirm the rmtfs process map corresponds to the real UIO device and that
   the daemon stays alive after modem ONLINE.
3. Read `/tmp/hostsvc-pd-listen.log`; require real SERVREG 0x42
   NEW_SERVER and a successful QMI 0x20 result=0/error=0.
4. Check QRTR server table for a real service 0x45 advertisement. If absent,
   continue modem/firmware-side diagnosis; do not fake it.
5. Only after real WLFW handshake and `/sys/class/net/wlan0` exists may the
   UI advertise Wi-Fi capability.


### Final artifact correction (2026-11-21)

The image was rebuilt once more after adding the final UIO address/size overflow
check in `host_services.c`. This final artifact supersedes the earlier hash
above:

- `vqeaf-init`: 11,144,504 bytes, 269 objects.
- `dist/boot-vqeaf-violet.img`: 32,108,544 bytes.
- SHA-256: `494fd87257659d4ae0a197c88301ccd10d4f2a113bc472e644c1d947c8280a79`.
- `adb devices -l` and `fastboot devices` returned no attached device; no flash
  or phone-side validation was performed.
---

## Đối chiếu nguồn violet + sửa Binder/CNSS + firmware audit (2026-11-21)

### 1. Nguồn tham chiếu đã kiểm tra

- **crdroidandroid/android_device_xiaomi_violet**, branch **14.0**:
  - BoardConfig.mk xác nhận BOARD_WLAN_DEVICE := qcwcn và WIFI_DRIVER_DEFAULT := qca_cld3.
  - wifi/WCNSS_qcom_cfg.ini là file cấu hình đúng của Violet, 14.458 bytes; SHA-256:
    de1961faac00fc6c004aea21c82f731bd2905c8db3e6026a8d1638d080acb3c9
  - rootdir/etc/init.qcom.rc định nghĩa chính xác service: /vendor/bin/cnss-daemon -n -l. Đã sửa đối số QEAF từ -n -dd thành -n -l để khớp cấu hình thiết bị.
- **crdroidandroid/android_kernel_xiaomi_violet**, branch **14.0**:
  arch/arm64/configs/vendor/sdmsteppe-perf_defconfig bật CONFIG_ANDROID_BINDER_IPC=y, CONFIG_ANDROID_BINDERFS=y, CONFIG_QRTR=y, CONFIG_QRTR_SMD=y, CONFIG_ICNSS=y, CONFIG_ICNSS_QMI=y và CONFIG_QCOM_QMI_HELPERS=y. Đây là cấu hình tham chiếu của nhánh kernel nguồn, không phải bằng chứng config của kernel đang chạy trên máy.
- **TheMuppets**: GitHub API trả HTTP 451 (DMCA block) với repo proprietary_vendor_xiaomi. GitLab mirror có branch lineage-18.1, nhưng cây branch đã clone không có thư mục violet/ hay sm6150-common/; lineage-20 không xuất hiện trong danh sách branch từ remote. Vì vậy vòng này **không lấy proprietary binary từ TheMuppets** và không thể xác nhận đó là blob mới nhất. Không thay bằng blob của thiết bị Qualcomm khác.

### 2. Firmware đã đối chiếu và đưa vào package

Nguồn hiện có trên máy vẫn là NON-HLOS **VIOLET_20.10.30** (weekly 31-10-2020), không phải ROM fastboot mới nhất. Thư mục build/violet-fw/image chứa 30 file BDWLAN* (base + biến thể), REGDB.BIN và WLANMDSP.MBN. Không có tên file OTP* hoặc QWLAN* trong nguồn này. Chưa có bằng chứng từ đúng firmware Violet rằng hai nhóm file đó là thành phần bắt buộc; không tự tạo hoặc lấy từ chip/điện thoại khác.

Đã phát hiện lỗi packaging: REGDB.BIN có trong nguồn nhưng package_violet.py trước đây chỉ copy BDWLAN* và WLANMDSP.MBN. Đã sửa để stage regdb.bin vào cả /lib/firmware/regdb.bin và /lib/firmware/wlan/qca_cld/regdb.bin; các file BDWLAN* cũng được stage ở cả hai đường dẫn. Manifest package kiểm tra đủ hai bản cùng WCNSS_qcom_cfg.ini trong qca_cld.

SHA-256 các file hiện được stage:

| File | Kích thước | SHA-256 |
|---|---:|---|
| bdwlan.bin | 26.328 B | 38ecb9a2d5ad40c210d5cd1edadb18a0b374fe6cb194c3c8130c3cfe212e9df7 |
| regdb.bin | 19.348 B | 6267f3e7bd62e1c4c676ee3ea2d7b6be9a9cd6ea7a794097a443e47467e0f59a |
| wlanmdsp.mbn | 3.694.736 B | 4c3d277812fe0496ee7f486be80b4dc530ebc2655eb18e1302b642857008d4b8 |
| WCNSS_qcom_cfg.ini | 14.458 B | de1961faac00fc6c004aea21c82f731bd2905c8db3e6026a8d1638d080acb3c9 |

WCNSS_qcom_cfg.ini vẫn được stage ở cả /vendor/etc/wifi/ và /lib/firmware/wlan/qca_cld/. cnss-daemon (95.024 B) và Bionic/QMI libraries tiếp tục được lấy từ stock dump đã trích xuất trước đó.

### 3. Sửa Binder đúng giao thức binderfs

Lỗi trong implementation cũ: mount binderfs xong lập tức tìm /dev/binderfs/binder. Kernel chỉ tạo binder-control khi mount; các device binder, hwbinder, vndbinder phải được cấp phát bằng ioctl BINDER_CTL_ADD.

Đã sửa platform/violet/host_services.c:
1. Đọc /proc/filesystems để xác nhận binderfs.
2. Mount binderfs tại /dev/binderfs và mở /dev/binderfs/binder-control.
3. Gọi BINDER_CTL_ADD (_IOWR('b', 1, struct binderfs_device)) cho từng device, sau đó xác nhận node char-device thực sự được tạo.
4. Publish /dev/binder, /dev/hwbinder và /dev/vndbinder bằng symlink tới các node binderfs; kiểm tra mở /dev/binder thành công.
5. Nếu kernel không hỗ trợ binderfs, chỉ fallback sang /sys/class/misc/binder/dev để lấy đúng major:minor từ sysfs. Đã bỏ hoàn toàn vòng dò minor 0..7; không còn đoán device number.
6. Nếu Binder chưa usable, CNSS không được exec; log trạng thái và dừng nhánh CNSS thay vì cố chạy rồi abort.

Kernel binderfs UAPI của nhánh tham chiếu xác nhận cấu trúc binderfs_device gồm name[256], major, minor và ioctl BINDER_CTL_ADD. Runtime hiện tại chạy trực tiếp trong initramfs root namespace, không tạo một chroot riêng; vì vậy binderfs được mount ngay trong /dev của rootfs mà daemon sử dụng. Nếu kiến trúc sau này chuyển CNSS vào chroot riêng thì mới cần bind-mount /dev hoặc các node Binder vào namespace đó.

### 4. Điều kiện chạy CNSS và giới hạn kiểm chứng

- CNSS dùng đúng tham số thiết bị -n -l; launcher hiện có chuyển stdout/stderr vào log host service.
- CNSS vẫn bị gate bởi MAC factory hợp lệ đọc từ persist p74. Không sinh MAC, không thay bằng địa chỉ từ serial và không bỏ gate calibration/NV.
- Không mock QRTR/QMI, không tạo service WLFW giả, không tự công bố 0x45. qrtr-lookup được khởi chạy để quan sát service thực sau khi daemon được exec.
- Tại thời điểm build này, máy phát triển không có COM serial, ADB device hoặc Fastboot device được kết nối. Do đó chưa thể chạy bản initramfs mới trên điện thoại, chưa thu log CNSS mới, chưa kiểm tra qrtr-lookup WLFW 0x45, dmesg hay trạng thái wlan0 mới. Báo cáo thiết bị gần nhất vẫn ghi wlan0 absent và factory MAC absent; không được diễn giải build thành bring-up thành công.

### 5. Build/package verification

- python scripts/build_violet.py — PASS; linked build/violet/vqeaf-init, 11.145.040 bytes, 269 objects.
- python scripts/package_violet.py --skip-build — PASS.
- Initramfs: 136 entries, gzip 14.601.501 bytes; boot image dist/boot-vqeaf-violet.img = 32.112.640 bytes.
- validate_bootimg.py — PASS.
- audit_rescue_channel.py — PASS.
- python scripts/verify_native_wifi_package.py — PASS; kiểm tra CNSS, Bionic linker/libs, bdwlan.bin, regdb.bin, wlanmdsp.mbn và WCNSS_qcom_cfg.ini trong CPIO.
- py_compile cho script package/verify và git diff --check — PASS.
- Không flash hoặc ghi bất kỳ partition nào.

**Trạng thái sau vòng này:** package đã có regdb.bin và Binder binderfs allocation đã được sửa đúng ioctl; CNSS chưa được xác nhận chạy trên thiết bị. Blocker còn lại để có log thực là boot bản image này trên Violet có UART/ADB/Fastboot kết nối, xác minh Binder node mở được, kiểm tra persist có factory MAC/calibration thật, rồi mới quan sát CNSS/QMI 0x45 và wlan0.


### Cập nhật package sau khi bổ sung đường dẫn qca_cld (2026-11-21, supersedes metrics above)

Đã cập nhật stage để copy từng file BDWLAN* và regdb.bin vào cả /lib/firmware/ và /lib/firmware/wlan/qca_cld/. WCNSS_qcom_cfg.ini đã có ở qca_cld từ bước trước. verify_native_wifi_package.py hiện kiểm tra đủ:
- lib/firmware/bdwlan.bin và lib/firmware/regdb.bin
- lib/firmware/wlan/qca_cld/bdwlan.bin và regdb.bin
- lib/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini
- vendor/etc/wifi/WCNSS_qcom_cfg.ini và wlanmdsp.mbn.

Kết quả package cuối cùng sau thay đổi này:
- Initramfs: 167 payload entries; gzip 14.644.279 bytes.
- dist/boot-vqeaf-violet.img: 32.157.696 bytes.
- SHA-256: 64c47c87e34703eca526946284f154111892c0b57e56937e726a147614a365bf.
- validate_bootimg.py: PASS; audit_rescue_channel.py: PASS.
- verify_native_wifi_package.py: PASS (168 CPIO entries, gồm trailer).
- Không flash hoặc ghi partition. Thiết bị chưa kết nối nên không có log CNSS/QRTR/WLFW mới; wlan0 chưa được xác nhận.



---

# Định hướng lại kiến trúc Wi-Fi — Linux thuần, không Bionic/APEX/Binder (2026-11-21)

## 1. Kiểm tra kernel config — kết quả đo được

### 1.1 Kernel đang chạy trên violet
- `/proc/version`: `4.14.215-azure-Pixel+`, built by Android clang 14.0.6 —
  **downstream stock kernel**, không phải mainline.
- `/sys/module` chứa `icnss`, `wlan` (qcacld-3.0), `cfg80211`, `bluetooth`,
  `binder`, `binder_alloc`, `service_locator`; **không có `ath10k` / `ath10k_snoc`**.
- dmesg: `icnss: Platform driver probed successfully`,
  `wlan_hdd_state wlan major(500) initialized` → chắc chắn **icnss + qcacld-3.0**.
- `/proc/config.gz` không đọc được; ROM dump chỉ có boot.img (đã compile),
  không có source/config 4.14 trong repo.

=> Chọn nhánh **stock downstream + daemon C thuần**, KHÔNG chuyển sang
ath10k_snoc trên thiết bị này.

### 1.2 Config liên quan có trong repo
| File | Nội dung liên quan |
| --- | --- |
| `platform/mt689x/kernel/qeaf_mt689x.fragment` | chỉ storage/BT/audio/touch/USB/fbdev; **không có QRTR/ATH10K/WLAN** |
| `temp/mt689x/chopin_defconfig` | `CONFIG_QRTR_SMD=m`, `CONFIG_QRTR_TUN=m`, `CONFIG_ATH10K=m`, `CONFIG_ATH10K_PCI=m`, `CONFIG_ATH10K_SDIO=m`, `CONFIG_ATH10K_SNOC=m`, `CONFIG_QCOM_WCNSS_PIL=m`, `CONFIG_QCOM_WCNSS_CTRL=m`, `CONFIG_SND_USB_AUDIO_QMI=m` |
| `temp/net_qrtr_Kconfig` | `config QRTR`, `QRTR_SMD`, `QRTR_MHI`, `QRTR_USB`, `QRTR_FIFO`, `QRTR_MHI_DEV`, `QRTR_USB_DEV`, `QRTR_ETHERNET` |

**Không có file config/defconfig nào cho violet 4.14 trong repo** — chỉ có
MT689x (khác SoC). Các symbol `ICNSS`, `CNSS`, `QCACLD` không xuất hiện trong
bất kỳ file config nào của repo (vì kernel stock là binary-only).

### 1.3 Về hướng mainline
Nguồn `linux-7.2.9`/`linux-7.3.4` có trong WSL (`/root/kbuild`) và đã từng
thử `fastboot boot` (xem mục trước): kernel boot được nhưng mất QRTR/DRM,
ath10k không probe đúng, không có `wlan0`. Việc switch kernel lên mainline
là dự án riêng, **không nằm trong phạm vi đính chính Wi-Fi lần này**.

## 2. Tầng bringup Wi-Fi thuần Linux (đã bắt đầu triển khai)

### 2.1 Nguyên tắc
- Không dùng `cnss-daemon`, Bionic linker64, APEX, binder, chroot.
- Mọi daemon đều là **aarch64-linux-musl static**, link trực tiếp với
  `platform/violet/hosttools/src/qrtr/lib` (libqrtr vendored).
- Không tạo QRTR server giả trong host test; server thật phát bằng
  `qrtr_new_server()` vào kernel QRTR name service.
- Không tạo MAC, BDF, calibration giả; đọc từ `/lib/firmware` hoặc persist thật.

### 2.2 Daemon `qwlfgw` — WLFW 0x45 server
- File: `platform/violet/hosttools/src/qwlfgw/qwlfgw.c`.
- Build: `scripts/build_violet_hosttools.py` target `qwlfgw`
  (đã build OK: `build/violet/hosttools/qwlfgw`, 1.051.160 B).
- Stage: `scripts/package_violet.py` đưa vào `/sbin/qeaf/qwlfgw` (tổng 9 daemon).
- Gọi thử: rescue verb `wifi native` → `vio_host_wlfw_native()` → `launch(qwlfgw)`.
- Hành vi hiện tại (foundation, chưa đạt `wlan0`):
  - `qrtr_open(0)` + `qrtr_new_server(sock, 0x45, 0, 0)` → phát service thật.
  - Nhận QMI request, parse header 7 byte, TLV walker tối thiểu.
  - `WLFW_IND_REGISTER 0x20`: nhớ client, trả QMI result 0/error 0.
  - `WLFW_CAP 0x24`: trả result 0 (chưa có capability TLV đầy đủ).
  - `WLFW_BDF_DOWNLOAD 0x25`: log file_id/seg, đánh dấu BDF đã nhận.
  - `WLFW_CAL_*`: placeholder có log rõ "not implemented".
  - `WLFW_MAC_ADDR 0x33`: **từ chối (result=1)** vì không có MAC factory thật —
    không bao giờ bịa MAC.
  - Sau CAP+BDF+IND_REGISTER: gửi `FW_READY_IND 0x21` tới các client đã đăng ký.
- Message ID lấy trực tiếp từ `hosttools/src/wlfwd/wlfwd.c:35-47` (không đoán).

### 2.3 Các bước còn thiếu để qwlfgw thành công năng thực
1. Đối chiếu TLV response của `WLFW_CAP` và `FW_READY_IND` với log của
   cnss-daemon thật / header `wlfw_service_v01.h` từ ROM MIUI dump. Hiện tại
   response chỉ có standard result TLV.
2. Implement CAL_DOWNLOAD/CAL_REPORT theo file_id thật, đọc
   `/lib/firmware/bdwlan.bin`, `regdb.bin`, và (nếu có) persist calibration.
3. Xử lý `WLFW_FW_MEM_READY_IND 0x37` / `FW_INIT_DONE_IND 0x38` theo thứ tự
   mà icnss mong đợi; thêm state machine + retry theo PD restart.
4. Tích hợp SERVREG listener (`pd-listen`) — hiện chưa có dòng `result=0 error=0`
   trên máy; phải sửa logic watch để xử lý cả server đã tồn tại trước khi
   pd-listen chạy (chỉ nghe NEW_SERVER là bỏ sót endpoint cũ).
5. Chạy `wifi native` trên máy, bắt `qrtr-lookup 0x45`, `/sys/class/net`,
   dmesg; chỉ khi `0x45` xuất hiện + firmware ready + `wlan0` thì coi là PASS.

## 3. Tầng quản lý kết nối (thiết kế)

### 3.1 Thành phần
| Thành phần | Nguồn | Vai trò |
| --- | --- | --- |
| `wpa_supplicant` | cần vendor source `hostap` (chưa có trong repo/WSL) | scan/connect WPA2, control socket `/run/wpa_supplicant/wlan0` |
| `udhcpc` | cần vendor `busybox` (chưa có trong repo/WSL) | DHCP client lấy IP sau khi associat |
| `qeaf-wifi` | mới, C thuần musl | daemon quản lý: mở wpa_ctrl, gọi scan/connect/status, giữ state, expose Unix socket `/run/qeaf/wifi.sock` |

### 3.2 Giao thức control socket `/run/qeaf/wifi.sock`
Yêu cầu dạng text, một dòng/ response (dễ debug từ Terminal):
- `SCAN` → `OK <bss_count>`
- `SCAN_RESULTS` → mỗi dòng `bssid\tssid\trssi\tsecurity`
- `CONNECT <ssid> <psk>` → `OK` / `FAIL <reason>` (PSK không được log)
- `DISCONNECT` → `OK`
- `STATUS` → `state=... ip=... rssi=... ssid=...`
- `POWER <on|off>` → `OK|FAIL`

`qeaf-wifi` dùng `wpa_ctrl_open()`/`wpa_ctrl_request()` từ hostap, không
chạy shell, không lưu password vào log.

### 3.3 Tích hợp Terminal
Mở rộng `build_terminal()` trong `platform/violet/ui/vqeaf_pages.c`:
- Lệnh `wifi scan` → gửi `SCAN`, `SCAN_RESULTS` qua socket.
- `wifi connect <ssid> <psk>` → gửi `CONNECT` (ẩn PSK khỏi output).
- `wifi status`, `wifi disconnect`, `wifi power`.
Dùng chung `vqeaf_terminal_run()`; KHÔNG gọi trực tiếp wpa_supplicant từ UI.

### 3.4 Tích hợp Settings LVGL
Trang Wi-Fi (`VQEAF_PAGE_WIFI`) hiện chỉ đọc `vqeaf_state`. Thêm:
- Nút "Quét" → gửi `SCAN`, sau 2-3 s gọi `SCAN_RESULTS`, render danh sách BSS.
- Mỗi BSS: SSID, RSSI, biểu tượng khoá; tap → màn hình nhập PSK (dùng IME có sẵn).
- "Kết nối" → `CONNECT`; "Ngắt" → `DISCONNECT`.
- Trạng thái IP/RSSI/SSID lấy từ `STATUS`, cập nhật通过 timer 5 s.
- Mọi thao đều qua `qeaf-wifi` control socket; UI không chạy process.

## 4. Trình tự triển khai & tiêu chí nghiệm thu
1. **P0** (đã làm): build + stage `qwlfgw`,rescue `wifi native`.
2. **P1**: chạy `wifi native` trên máy → `qrtr-lookup 0x45` phải thấy server;
   cnss-daemon/Bionic KHÔNG được khởi động.
3. **P2**: hoàn thiện state machine WLFW theo header ROM, đạt `wlan0`.
4. **P3**: vendor hostap + busybox, build static musl, stage vào ramdisk.
5. **P4**: viết `qeaf-wifi` + control socket; host test với wpa_supplicant giả lập.
6. **P5**: Terminal + Settings gọi socket; kiểm chứng scan thật trên máy.
7. **P6**: ổn định modem (reset 40-60 s) — vấn đề độc lập phải giải quyết riêng.

**Không đánh dấu PASS** khi chưa có: `0x45` thật, firmware ready, `wlan0`,
scan results. Mọi log mẫu trong tài liệu này là evidence đã đo, không phải
kết quả giả lập.


## RAM-only test session — tmpfs persist + TEST MAC (2026-11-22)

### Yêu cầu và rào đã tuân thủ
- **Không ghi phân vùng vật lý**: tmpfs được **chồng (stacked mount)** lên
  `/mnt/vendor/persist`; p74 vẫn còn đó bên dưới nhưng bị che, không bị ghi.
- **Không lưu dữ liệu cố định**: MAC dùng cho phiên test là
  `02:1A:2B:3C:4D:5E` (locally administered, do operator cung cấp), chỉ tồn tại
  trong RAM, mất khi reboot. Mọi output đều ghi rõ `RAM-ONLY` / `NOT factory`.
- Không đăng ký QMI server giả, không trả success giả, không tạo calibration
  giả. `qwlfgw` (placeholder) **không** được khởi chạy.

### Lệnh và bằng chứng thật trên máy
```
wifi ramtest
  persist-ram: RAM-ONLY tmpfs at /mnt/vendor/persist; test MAC
  02:1A:2B:3C:4D:5E (session only, NOT factory); file
  /mnt/vendor/persist/wlan_mac.bin Intf0..Intf3+END; p74 untouched
  wifi ramtest: cnss/wlfw launched in RAM-only session

wifi cat /mnt/vendor/persist/wlan_mac.bin
  Intf0MacAddress=021A2B3C4D5E
  Intf1MacAddress=021A2B3C4D5F
  Intf2MacAddress=021A2B3C4D60
  Intf3MacAddress=021A2B3C4D61
  END

wifi find wlan_mac.bin  -> /mnt/vendor/persist/wlan_mac.bin
wifi state
  hostsvc cnss-daemon: running pid=539
  hostsvc binder: present
  hostsvc wlan: CNSS daemon spawned by QEAF service manager (-n -l)
```

### Kết quả đo (BLOCKED, không PASS)
| Hạng mục | Kết quả thật |
| --- | --- |
| tmpfs rw persist + MAC test | **VERIFIED RAM-only** (`wifi find` thấy file, `cat` đúng 5 dòng) |
| `find_factory_mac()` đọc format Qualcomm | **VERIFIED** (sửa `read_mac()` buffer 64→512 + parser `Intf0MacAddress=`) |
| cnss-daemon thật chạy + binderfs | **VERIFIED** (`pid=539`, `binder: present`) |
| `wlfw_cal_*.bin` sinh ra trên tmpfs | **FAILED — không có file nào** (`wifi find wlfw_cal_00/01.bin: not found`) |
| QMI `WLFW 0x45` server | **BLOCKED** — `wifi discover`: `recv_calls=0`, `NOT_REGISTERED`, `rc=-2 (ENOENT)` |
| `qrtr-lookup 0x45` | `lookup 0x45/0x1/0x0 sent`, sau đó `recvfrom(): Resource temporarily unavailable` (timeout) |
| log cnss-daemon | Chỉ có `linker: Warning: failed to find generated linker configuration from "/linkerconfig/ld.config.txt"` — **không có log QMI/cold-boot** |
| `wlan0` | **absent** (`ip link show wlan0`: không có) |

### Kết luận trung thực
Việc cấp MAC hợp lệ trong RAM **đủ để `find_factory_mac()` thành công** và để
CNSS daemon thật khởi động, nhưng **chưa đủ** để daemon đăng ký `0x45` hay để
firmware chạy Cold Boot Calibration. Không có `wlfw_cal_*`, không có `wlan0`.
→ Blockers còn lại nằm ở firmware/ROM daemon (thiếu property Android, service
manager, hoặc WLAN PD chưa lên), **không phải** ở MAC hay ở việc chưa launch
daemon. Không được dùng kết quả này để tuyên bố Wi-Fi hoạt động.

Dữ liệu RAM-only không là bằng chứng phần cứng vĩnh viễn; reboot là sạch.


## Tiếp tục Wi-Fi: phân tích binary cnss-daemon thật (2026-11-22)
### Bằng chứng từ binary ROM `build/violet/hosttools/cnss-daemon` (95.024 B)
Trích chuỗi ASCII trực tiếp (tools `strings` của MinGW không đọc được ELF
aarch64, dùng Python regex thay):
- `usage: %s [options]` → `-n, --nodaemon  do not run as a daemon`,
  `-l  Log output to logcat`. **Cờ `-n` KHÔNG tắt service WLFW** — chạy đúng
  như init rc của vendor.
- `Failed to start wlfw service`, `WLFW service connected`,
  `wlfw_send_ind_register_req`, `qmi_client_get_any_service`,
  `Sending DMS get mac address` → cnss-daemon là **QMI client** của WLFW/DMS
  (service do modem firmware cung cấp), không phải server `0x45`.
- Đường dẫn calibration thật: `/data/vendor/wifi/wlfw_cal_%02d.bin`,
  `/data/vendor/wifi/qdss_trace_config.bin` — **không phải**
  `/mnt/vendor/persist/wlan/`. Đây là điểm từng bị bỏ sót.
- Firmware: `/vendor/firmware/`, `/vendor/firmware_mnt/image/`,
  `bdwlan.bin`, `bdwlan.b%02x`, `bdwlan.%03x`, `qdss_trace_config.cfg`.

### Thay đổi và đo lại (RAM-only)
- `vio_host_persist_ram_setup()` giờ tạo thêm `/data/vendor/wifi` trên
  initramfs (RAM), không ghi p74/userdata vật lý.
- Sau flash (sha `b6b8e62b77cf6ba01169dfed2e968ccffe2706cc59d9a7f375229ddd352d91c0`):
  `wifi ramtest` báo `/data/vendor/wifi RAM dir ready`; `wifi ls
  /data/vendor/wifi` tồn tại nhưng **rỗng** — không có `wlfw_cal_*.bin`.
  `cnss-daemon: running pid=545`, `binder: present`, nhưng
  `wifi discover` vẫn `recv_calls=0 / 0x45 NOT_REGISTERED / ENOENT`.

### Kết luận
Việc tạo đúng thư mục calibration là cần thiết nhưng **chưa đủ**. Không có
service `0x45` thật từ modem, cnss-daemon ở trạng thái client không tìm thấy
service → không sinh calibration, không có `wlan0`. Không đăng ký server giả,
không trả QMI response giả. BLOCKED giữ nguyên.


## Bằng chứng SERVREG / WLAN PD thật (2026-11-22)
- `pd-listen` chế độ watch tra cứu `0x42` instance **0**, trong khi QRTR name
  service báo server thật là `0x42/0x4c@node 10:port 3` và `0x42/0x4a@5:3`.
  Name service khớp **chính xác instance**, nên lookup instance 0 không bao giờ
  nhận được NEW_SERVER → log chỉ có dòng "watching", không có result/error.
- Chạy chế độ trực tiếp với node/port thật:
  `wifi listen 10 3 msm/modem/wlan_pd` →
  ```
  pd-listen: msm/modem/wlan_pd via servreg 10:3 result=1 error=45
  ```
  → **WLAN PD đang ở trạng thái lỗi thật**, không phải thiếu descriptor.
  (`MODEMUW.JSN` trong firmware_mnt có `msm/modem/wlan_pd`, qmi_instance 180.)
- Yêu cầu restart PD thật qua `pdr-restart`:
  `wifi pd 10 3 msm/modem/wlan_pd` →
  ```
  restart msm/modem/wlan_pd: result 1 error 9
  ```
  → Restart cũng bị kernel/modem từ chối.
- Sau đó `wifi discover` vẫn `recv_calls=0 / 0x45 NOT_REGISTERED / ENOENT`.

### Kết luận
Blocker giờ được khoanh vùng chính xác: **WLAN PD (msm/modem/wlan_pd) không
lên**, SERVREG báo `result=1 error=45`, restart trả `result=1 error=9`. Khi PD
không lên, firmware WCN3990 không boot và không publish QMI `0x45`, nên không
có `wlan0`. Không tạo server/QMI response giả; cần firmware/PD thật từ phía
modem (ngoài khả năng sửa bằng userspace trên initramfs hiện tại).


## Boot order / firmware sources / full modem restart (2026-11-22)
### 1. Boot order và log kernel
- Trước `wifi up`: `pd-mapper`, `pd-listen`, `tqftpserv`, `rmtfs` đã chạy
  (`wifi state` pid 484..487). `rmtfs` log: `RMTFS UIO ready ... registering
  services`; `pd-mapper`: `fallback found 12 PD json files`, `loaded 18
  domain/service entries`; `tqftpserv` phục vụ `/lib/firmware`.
- Mount theo `/proc/mounts`: `/vendor` p86 ro, `/vendor/firmware_mnt` p82 ro,
  `/mnt/vendor/persist` p74 ro, `/vendor/bt_firmware` p38 ro. rmtfs/tqftpserv
  chạy trước modem boot đúng thứ tự.
- Kernel log (`wifi dmesg`, 56 dòng lọc) có `subsys-pil-tz` cho adsp/cdsp/venus
  ONLINE, `icnss` probe, `wlan_hdd_state wlan major(500) initialized`.
  Không có dòng `request_firmware` / `wlanmdsp` / `q6v5` / `mpss` / `servreg`
  trong log lọc — firmware WLAN không được yêu cầu nạp trong phiên này.
- `/sys/class/remoteproc` **không tồn tại**; kernel dùng `msm_subsys`
  (`subsys0=adsp ONLINE`, `subsys1=modem OFFLINING`, `subsys2=cdsp ONLINE`,
  `subsys3=venus ONLINE`, `subsys4=ipa_fws OFFLINING`, `subsys5=a612_zap
  OFFLINING`). Modem ở **OFFLINING**, `restart_level=SYSTEM`, `crash_count=0`.

### 2. Nguồn firmware
- `violet-fw.zip`: `NON-HLOS.bin` (modem), `BTFM.bin`, `dspso.bin`, các bản vá
  bootloader/TZ; `build/violet-fw/image/` có `WLANMDSP.MBN` (3.694.736 B),
  28+ `BDWLAN.*`, `regdb.bin`, và 6 `.JSN` SERVREG descriptors.
- `out/reference/stock-violet/` **không tồn tại** trong workspace → không có
  bản chuẩn để đối chiếu phiên bản modem/dsp. Trên máy, `/vendor/firmware_mnt/
  image/` có đủ `modem.b00..b29`, `modem.mdt`, `adsp.*`, `cdsp.*`, `venus.*`.
  Không phát hiện file WLAN bị thiếu; không thể khẳng định firmware cũ/khớp
  khi thiếu bản reference.

### 3. Restart toàn bộ modem
- Không có `remoteproc` sysfs để ghi state. `msm_subsys` chỉ có `state`,
  `restart_level`, `firmware_name`, `crash_count`, `uevent` — **không có node
  `restart`** có thể ghi trực tiếp.
- QMI/SERVREG `pdr-restart` thật với node/port SERVREG `10:3`:
  - `msm/modem/wlan_pd` → `result 1 error 9`
  - `msm/modem/root_pd` → `result 1 error 9`
  → Cả restart PD riêng lẫn restart toàn bộ modem đều bị từ chối.
- `pd-listen` trực tiếp vẫn báo `result=1 error=45` cho `msm/modem/wlan_pd`.

### Kết luận
Các daemon userspace chạy đúng thứ tự, firmware cần thiết có mặt, nhưng modem
ở trạng thái **OFFLINING** và mọi yêu cầu restart PD/root_pd qua SERVREG đều bị
từ chối (`error 9`). Đây là blocker tầng modem/firmware, không phải lỗi boot
order hay thiếu file. Không mock QMI, không server giả, không partition write.
