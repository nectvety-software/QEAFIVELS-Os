# VIOLET — Source & Firmware Provenance (Phase 1)

Ngày: 2026-11-22. Mục đích: ghi lại **nguồn gốc pháp lý và kỹ thuật** của mọi mã
và firmware sẽ dùng cho Redmi Note 7 Pro (`violet`), trước khi viết bất kỳ driver
native nào. Không có file nguồn/firmware nào bị tải về hoặc trích xuất trong
bước này; các SHA-256 dưới đây là của **artifact đã có sẵn trong workspace**.

Nguyên tắc:
- Không đính kèm binary proprietary vào image/OS package.
- Không trộn firmware giữa các vùng (India/China) hoặc giữa thiết bị.
- Mọi thứ chưa xác minh được hash/nguồn → đánh dấu **UNVERIFIED**.
- Mã GPL tái sử dụng phải giữ GPL cho phần phái sinh và ghi rõ file nào.

## 1. Mã nguồn kernel / platform

| # | Nguồn | URL / repo | Phiên bản | License | SHA-256 | Tái sử dụng | Viết lại | Trạng thái |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| S1 | **Xiaomi OSS violet-q-oss** (kernel downstream Android Q) | `github.com/MiCode/Xiaomi_Kernel_OpenSource` → branch `violet-q-oss`, commit `1488fa3d05e2f9be5fc62e0bb16ad85d96db2076` | Linux `4.14.117`; tag Qualcomm `LA.UM.8.9.r1-04400-SM6xx.0-1` | GPL-2.0 | commit + SHA-256 file đã pin trong §7; **chưa khớp binary stock** | Thuật toán/probe của DSI (nt36672a), touch nt36xxx, WCD937x, SDHCI, DWC3, WCN3990/icnss | toàn bộ lớp OS API (device model, IRQ, DMA, PM, firmware loader) | Candidate base cho backport; không được coi là source của `4.14.215-azure-Pixel+` |
| S2 | **Xiaomi OSS violet-p-oss** (Android P) | cùng repo → branch `violet-p-oss` | `LA.UM.7.9.r1-03500-sm6150.0` (E1) | GPL-2.0 | **UNVERIFIED** | Đối chiếu khi q-oss thiếu file; tham chiếu DT gốc | — | Chỉ tham chiếu |
| S3 | **LineageOS kernel sm6150** | `github.com/LineageOS/android_kernel_xiaomi_sm6150` | branch `lineage-24.0`; wiki ghi kernel **4.14** (E1) | GPL-2.0 | **UNVERIFIED** | Bản kernel gần mainline nhất cho sm6150; tham chiếu binding + driver đã dọn | không dùng trực tiếp làm base | Chưa tải |
| S4 | **LineageOS device tree violet** | `github.com/LineageOS/android_device_xiaomi_violet` | branch `lineage-23.2`, 3.655 commit (E1) | Apache-2.0 (header `device.mk` ghi `SPDX-License-Identifier: Apache-2.0`) | **UNVERIFIED** | `configs/` (audio/mixer/thermal), `rootdir/etc`, danh sách `proprietary-files.txt` để biết cần blob nào | toàn bộ phần Android (init/HAL/sepolicy) **không** port | Chưa tải |
| S5 | **CAF msm-4.14** (Qualcomm) | `android.googlesource.com/kernel/msm` | commit tham chiếu `ef85d528056e...` cho binding WCN3990 (E1) | GPL-2.0 | **UNVERIFIED** | Binding `qcom,wcn3990-wifi` (SNOC + CE IRQ + memory-region) | — | Đã dùng để mô tả driver trong repo |
| S6 | **Linux mainline ath10k (qmi/wcn3990)** | `git.kernel.org` → `drivers/net/wireless/ath/ath10k/*` | nhánh mainline hiện hành (E1: driver `qmi_wcn3990` tồn tại) | GPL-2.0 | **UNVERIFIED** | Thuật toán CE, QMI/WLFW handshake, copy-engine ringing | thay toàn bộ netdev/skbuff bằng network stack của QEAF (chưa có) | Phase 6 |
| S7 | LineageOS Wiki (violet) | `wiki.lineageos.org/devices/violet/`, `/fw_update/` | nội dung 2025-09 | CC BY-SA 3.0 | — | Thông số thiết bị, phiên bản firmware India/China | — | Đã dùng (E1) |
| S8 | xmfirmwareupdater / mifirm | `xmfirmwareupdater.com/archive/miui/violet/`, `mifirm.net/model/violet.ttt` | — | trang tham chiếu | — | Chỉ đối chiếu tên bản MIUI | — | **Không tải file** |
| S9 | **AxionAOSP Violet kernel** | `github.com/AxionAOSP-devices/android_kernel_xiaomi_violet` → branch `lineage-23.2`, commit `057098841e8bc82f87c3ae5fb11eb7c956416f15` | Linux `4.14.357-openela` | GPL-2.0 | commit + SHA-256 file/patch trong §7 | Ba fix DRM/MSM bus đã review; binding/config/DTS chỉ dùng để đối chiếu | Android HAL, vendor blob, scheduler tuning, fingerprint TEE rewrite và toàn bộ kernel tree | **Tham chiếu/backport candidate**, chưa áp dụng production |

Các nhánh/phiên bản trên đều đã kiểm tra tồn tại; S1 và S9 đã pin commit cùng
SHA-256 của từng file dùng để backport. Việc pin commit **không** chứng minh source
khớp binary stock `4.14.215-azure-Pixel+`; xem §7 cho kết quả apply-check và giới hạn.

## 2. Firmware — nguồn và hash đã đo

Package nguồn: `build/violet-fw.zip` (firmware-update của bản MIUI, KHÔNG phải
file tải mới trong bước này).

```
build/violet-fw.zip                      69.667.787 B
  sha256 32c64413aec171d590cb597c5e293a7a2532555837386e16dae58ab7e7b8f15e
  members 19, uncompressed 158.605.828 B
```

| Nhóm | File | Kích thước | SHA-256 | Nguồn | Ghi chú |
| --- | --- | --- | --- | --- | --- |
| Modem/Toàn bộ radio | `firmware-update/NON-HLOS.bin` | 114.640.384 | `96833a4bcdc10dd3ff1f6700f0f837cdcb4bd54cf17d7f8803e32c65ce45f9ff` | ZIP member | Chứa cả WLAN image (không phải file rời) |
| Bluetooth | `firmware-update/BTFM.bin` | 897.024 | `8a6d1ab5a8aa4aecffee2021f320daf9a92ba0986af3b4b8a64a8a4af2f2e3e0` | ZIP member | Ghi vào p38 |
| DSP | `firmware-update/dspso.bin` | 33.554.432 | `d1c00b9d3e0063bf82b92a1f3767ec8e58c25129d516c6bd57a27c536b71a754` | ZIP member | ADSP/CDSP |
| Camera | `firmware-update/CAMERA_ICP.elf` (theo audit cũ) | 3.614.168 | **UNVERIFIED** (chưa hash lại) | ZIP/vendor | Cần hash lại trước khi dùng |
| Bootloader/TZ | `xbl.elf`, `abl.elf`, `tz.mbn`, `hyp.mbn`, `devcfg.mbn`, `cmnlib*.mbn`, `aop.mbn` | 43.576 – 3.162.984 | xem `firmware/violet/manifest.json` | ZIP member | **Không flash** (chính sách §13) |

Firmware đã trích (thư mục `build/violet-fw/image/`, 271 file — dùng cho bản dựng
hiện tại, stage vào `/lib/firmware`):

| File | Kích thước | SHA-256 | Trạng thái |
| --- | --- | --- | --- |
| `image/wlanmdsp.mbn` | 3.694.736 | `4c3d277812fe0496ee7f486be80b4dc530ebc2655eb18e1302b642857008d4b8` | **VERIFIED (local)** |
| `image/regdb.bin` | — | `6267f3e7bd62e1c4c676ee3ea8d7b6be9a9cd6ea7a794097a443e47467e0f59a` | **VERIFIED (local)** |
| `image/BDWLAN.*` | 30 biến thể | manifest ZIP + hash từng file khi cần | **VERIFIED (local)** — chọn đúng biến thể theo board data còn **UNVERIFIED** |
| `image/ADSP.B00` | 1.012 | `535a90749b2c8b254af2e4cd8ffd80f0f05c43a6183cbaa85df6c1ddf314ddf5` | VERIFIED (local) |
| `image/ADSP.*`, `MODEM.*`, `CDSP.*`, `CPE_9340.*`, `VENUS.*`, `HAVENTKN.*` | 271 file tổng | xem tool audit | Đã có, chưa dùng cho driver native |

Manifest máy đọc: `firmware/violet/manifest.json` (sinh bởi
`scripts/wifi/inspect_violet_firmware.py`, 19 member, `required_present`
wlanmdsp/bdwlan/regdb = `false` trên ZIP **là đúng** vì chúng nằm trong
`NON-HLOS.bin`).

**Tool hiện có:** `scripts/wifi/inspect_violet_firmware.py` (read-only, `--package`,
`--out`). Không tạo bản trùng ở `scripts/firmware/`; mọi mở rộng (TAR/TGZ, so
sánh phiên bản, phát hiện thiếu/sai nền tảng) phải bổ sung **vào file này**.

## 3. Firmware còn thiếu (blocker thật)

| File | Cần cho | Trạng thái |
| --- | --- | --- |
| `wlan_mac.bin` (p74, per-unit) | MAC Wi-Fi thật | **KHÔNG CÓ** — không được tự sinh MAC |
| `wlfw_cal_00..04.bin` (p74) | calibration WCN3990 | **KHÔNG CÓ** |
| `bdwlan.bin` đúng biến thể board | board data | có 30 biến thể nhưng chưa xác định biến thể của máy này |
| Sensor mode/OTP registers | camera | **UNVERIFIED** |
| GNSS aiding data | GNSS | chưa có |
| SLPI firmware (sensors) | sensor subsystem | chưa xác minh có trong package |

## 4. Mã trong repo — bản quyền và nguồn

Thống kê SPDX trong `platform/violet`, `kernel/`, `platform/violet/hosttools/src`:

| License | Số file | Ghi chú |
| --- | --- | --- |
| MIT | ~107 | mã QEAF tự viết (`wcn3990_snoc.c`, `wcn3990.h`, hosttools) |
| BSD-3-Clause | 38 | một phần hosttools/thư viện |
| GPL-2.0 / GPL-2.0-only | 22 | phần phái sinh từ kernel/Qualcomm — **phải giữ GPL** |

| Thành phần trong repo | Nguồn gốc | Trạng thái |
| --- | --- | --- |
| `kernel/drivers/wcn3990_snoc.c`, `wcn3990_phase2.c`, `kernel/include/vqeaf/wcn3990.h` | tự viết (MIT), dựa trên binding S5 | **Partial** — không có QMI/WLFW/firmware boot/netdev |
| `platform/violet/qrtr_ns.c` | tự viết (MIT) | Chạy thật trên máy (E3) |
| `platform/violet/hosttools/src/{rmtfs,tqftpserv,pd-mapper,qrtr,qwlfgw}` | port từ Qualcomm open-source (BSD-3-Clause/GPL tuỳ file) | Chạy thật; `qwlfgw` mới là partial |
| `platform/violet/{drm_kms.c,display_hal.c,usb_lun.c,vqeaf_camera.c}` | tự viết, dùng kernel UAPI (MIT) | Chạy thật trên kernel stock |
| `platform/violet/hosttools/cnss-daemon` | **binary từ ROM** | Chỉ dùng để chẩn đoán; **không** đóng gói vào OS |

## 5. Ràng buộc license / pháp lý

1. **GPL-2.0**: mọi file port từ S1–S3 phải giữ header GPL và ghi nguồn; không
   trộn vào file MIT mà không đổi license tương ứng.
2. **Blob proprietary** (NON-HLOS/BTFM/dspso/CAMERA_ICP): **không** phát hành lại
   trong repo/image; chỉ lưu hash + đường dẫn nguồn. Việc stage vào
   `/lib/firmware` trên máy là dùng nội bộ, không phân phối.
3. **LineageOS wiki** (CC BY-SA 3.0): chỉ trích dẫn thông tin, không copy văn bản
   dài vào repo.
4. **Không** tải firmware mới từ mifirm/xmfirmwareupdater trong bước này.

## 6. Việc bắt buộc cho Phase 2 (để chuyển UNVERIFIED → VERIFIED)

1. Clone `violet-q-oss` (đúng tag `LA.UM.8.9.r1-04400-SM6xx.0-1`) và ghi
   `git rev-parse HEAD`.
2. Clone `android_device_xiaomi_violet` branch `lineage-23.2` + ghi HEAD.
3. Hash mọi file DTS/DTSI sẽ port; thêm bảng "file nguồn → file đích" vào tài liệu này.
4. Hash `CAMERA_ICP.elf` và liệt kê đủ 271 file firmware với SHA-256 (tool hiện có).
5. Chốt biến thể `BDWLAN` của máy này bằng board ID thật (không đoán).

## 7. AxionAOSP driver backport candidates (2026-11-26)

Nguồn Axion được theo đúng manifest thiết bị: `AxionAOSP-devices` chứa kernel,
device tree và vendor tree Violet; `AxionAOSP/android_hardware_xiaomi` chỉ chứa
Android HAL/AIDL và không phải driver Linux. Kernel Axion đã pin ở S9; đây là bản
`4.14.357-openela`, gần họ nhưng **không đồng nhất** với stock `4.14.215-azure-Pixel+`.

Bộ patch tối thiểu nằm ở `platform/violet/kernel-patches/axion/`:

| Patch | Nội dung | Nguồn Axion | Trạng thái |
| --- | --- | --- | --- |
| `0001-msm-bus-skip-qos-clocks-when-none.patch` | Không bật QoS clocks khi client không có clock QoS | `f0861798d3f8ad3fd5a168d4caf6cfe037795dbc` | Apply-check PASS trên S1 |
| `0002-drm-msm-clear-drvdata-on-bind-failure.patch` | Xóa `platform_drvdata` khi DRM bind lỗi | `88a317d56295586d849bf2816f5e7b4b7c4b7257` | Logic backport, apply-check PASS trên S1 |
| `0003-drm-msm-dsi-clear-esd-only-on-active-change.patch` | Chỉ clear ESD recovery khi CRTC đổi trạng thái active | `111a6a5c490ff21a28bec4aab99527c48e29d20c` | Context backport, apply-check PASS trên S1 |

`provenance.json` ghi SHA-256 của từng patch/file nguồn, commit, license và trạng
thái kiểm tra. `scripts/check_violet_axion_backports.py` kiểm tra hash/path/base;
`--source <kernel-worktree> --apply` chỉ ghi patch sau khi mọi gate PASS.

Kết quả thực tế: `git apply --check` rồi `git apply` PASS trên đúng ba file S1;
`python -m unittest tests.test_violet_axion_backports -v`: **5/5 PASS**. Chưa build
kernel, chưa test QEMU và chưa test phần cứng vì thiếu source/config/DTB production
khớp binary. Không patch nào được áp dụng vào kernel payload stock hoặc image QEAF.

Không đưa vào bộ patch: patch PPS-delay có fallback ghi đè giá trị sau lỗi parse,
thay đổi CPU affinity của DRM worker, fingerprint TEE rewrite, DTS/config nguyên
bản Axion và mọi vendor blob/HAL Android. Wi-Fi/Bluetooth vẫn giữ trạng thái theo
bằng chứng firmware/service hiện có; ba patch trên không tuyên bố sửa các blocker đó.
