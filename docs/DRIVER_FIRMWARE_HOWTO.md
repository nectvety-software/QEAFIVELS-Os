# Cách tạo driver + firmware cho Redmi Note 7 Pro (violet)

Tài liệu tham khảo kiến trúc: `D:\os-system\hakios-xnu-kernel\drivers`
(HakiOS — port XNU/Darwin lên điện thoại Qualcomm). HakiOS chạy trên SM7325
(POCO X5 Pro), **không phải violet**, nên chỉ lấy *cách tổ chức và kỷ luật
chứng minh*, không chép mã hay địa chỉ phần cứng của họ.

Mục tiêu: thêm một driver mới cho violet vào QEAFIVELS OS mà vẫn giữ nguyên
nguyên tắc của repo này — chỉ báo trạng thái thật, không mock thay phần cứng,
không đưa firmware lạ vào ảnh.

---

## 1. HakiOS làm gì (và vì sao đáng bắt chước)

| Cơ chế | Cụ thể trong `hakios-xnu-kernel/drivers` | Điều đáng học |
| --- | --- | --- |
| Chia theo subsystem | `drivers/wifi/`, `audio/`, `power/`, `storage/`, `input/`, `camera/`, `gpu/`, `modem/`, `net/`, `bluetooth/`, `core/` | Mỗi thiết bị một thư mục theo chức năng, không theo tên chip |
| Danh mục tường minh | `drivers/map` liệt kê từng file + đường dẫn trong cây XNU; `export.sh` **từ chối** file mới không có trong map | Không có file "mồ côi": thêm file là phải thêm dòng khai báo |
| Cấu hình theo driver | `drivers/<subsystem>/conf`: `driver WIFI` + `file ...` + `requires QMI` + `help ...` | Bật/tắt từng driver bằng `CONFIG_HAKIOS_<NAME>`, có phụ thuộc rõ ràng |
| Đầu file tự khai độ tin cậy | `hakios.wifi.c` mở đầu bằng "Every address below belongs to this phone…"; mỗi driver ghi rõ *đã biên dịch / đã chạy QEMU / đã chạy trên máy / là phỏng đoán* | Người đọc biết ngay bằng chứng tới đâu |
| Thang bước có boot-arg | `wifi_step=N` chạy bước 1..N rồi dừng; mỗi bước nói *sắp chạm gì* trước khi chạm, **đọc trước khi ghi**, thiếu dữ kiện thì dừng chứ không đi mò | Chẩn đoán được trên máy thật mà không phải "chạy hết rồi đoán" |
| Không mặc định bật phần cứng | "nothing here is touched unless a boot-arg asks for it" | Boot thường không tự bật modem/DSP/radio |
| Firmware không nằm trong repo | Firmware lấy từ `/vendor/firmware_mnt/image` của chính máy; `fw-wifi/`, `fw-audio/`, `fw-gpu/` do người dùng tự đặt; `tools/provenance.allow` theo dõi ngoại lệ | Không phân phối blob của hãng; mọi thứ nạp vào máy đều có nguồn |
| Stand-in phải khai là stand-in | `hakios.wlfw.c`: "A STAND-IN, TO BE THROWN AWAY when ath11k's own qmi.c runs here" | Không giả vờ đã có driver thật |
| CI kiểm cấu trúc | `tools/drivers-check.py`: map ↔ thư mục khớp, không trùng, không giữ hai nơi | Tài liệu và mã không lệch nhau |

---

## 2. Đối chiếu sang QEAFIVELS (violet)

| HakiOS | QEAFIVELS hiện có | Ghi chú |
| --- | --- | --- |
| `drivers/<subsystem>/` | `kernel/drivers/` + `kernel/include/vqeaf/` | Driver port được, chạy được cả trong kernel freestanding và host test |
| `drivers/map` + `conf` | `CMakeLists.txt`, `scripts/build_kernel_aarch64.py`, `scripts/build_violet.py` (danh sách `SOURCES`) | Thêm file là phải thêm vào đúng danh sách build |
| Đầu file khai độ tin cậy | Quy ước có sẵn: comment đầu file nói rõ cái gì đã kiểm, cái gì là giả định | Giữ nguyên khi viết driver mới |
| `wifi_step=N` | Verb rescue `wifi …` + `wifi state`/`wifi fw`/`wifi diag`; `status` | Kênh QEAFRESCUE là nơi chẩn đoán trên máy thật |
| Backend phần cứng do platform cấp | `platform/violet/wifi_bringup.c`, `wifi_fw_check.c`, `vio_caps.c`, `main_violet.c` | Đây là "PID 1 trên kernel stock", không phải kernel tự viết |
| `fw-*` do người dùng đặt | `firmware/violet/manifest.json` + `build/violet-fw/image` + `scripts/package_violet.py` stage vào `/lib/firmware` | Có SHA-256 từng file, in ra bằng `wifi fw` |
| `tools/drivers-check.py` | `tests/wifi/*.py`, `scripts/wifi/run_hal_tests.py`, `tests/*.py` | Test đọc source để chặn "thành công giả" |

Hai tầng cần phân biệt khi làm cho violet:

1. **Tầng driver (port được)** — `kernel/drivers/...`: logic probe/lifecycle,
   kiểm tra container firmware, state machine. Chạy được trong host test.
2. **Tầng platform (chạy trên máy)** — `platform/violet/...`: mount phân vùng,
   mở `/dev`, ghi sysfs, chạy daemon phụ, nhận lệnh từ QEAFRESCUE.

Một tính năng chỉ được coi là "có" khi cả hai tầng khớp nhau và có log thật.

---

## 3. Quy trình 7 bước tạo một driver mới

Ví dụ dưới đây là khung cho một thiết bị mới (Bluetooth WCN3990 trên p38,
camera, ADSP…); Wi-Fi đã có sẵn và là mẫu hoàn chỉnh nhất.

### Bước 1 — Chốt phạm vi bằng bằng chứng

Trước khi viết dòng nào, ghi vào đầu file những gì **đã đo được** trên violet:
bus (SNOC/PCIe/UART/I2C), địa chỉ MMIO, IRQ, phân vùng firmware, tên service.
Nếu chưa đo được thì ghi "chưa xác minh" — đừng lấy số từ điện thoại khác.

Nguồn hợp lệ: `docs/VIOLET_HARDWARE.md`, `docs/DRIVERS_VIOLET.md`,
`docs/platforms/*`, device-tree của chính máy, và log `wifi diag`/`status`.
**Không** lấy địa chỉ từ HakiOS (SM7325) — sai SoC là sai hết.

### Bước 2 — Khai báo hợp đồng trong header

`kernel/include/vqeaf/<ten>.h`: mô tả tài nguyên do platform cấp và các
callback bắt buộc. Không hard-code địa chỉ trong driver.

```c
/* SPDX-License-Identifier: MIT */
/* kernel/include/vqeaf/vio_bt.h - WCN3990 Bluetooth, violet.
 * Verified on violet: p38 (VFAT, ro) chứa BTFM.bin; UART qua blsp1_uart1;
 * CHƯA xác minh: chân BT_EN/clock, firmware NVM. */
#ifndef VQEAF_VIO_BT_H
#define VQEAF_VIO_BT_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    void *ctx;
    int  (*power_on)(void *);            /* regulator + clock + BT_EN */
    void (*power_off)(void *);
    int  (*uart_open)(void *, uint32_t baud);
    void (*uart_close)(void *);
    int  (*send)(void *, const uint8_t *buf, size_t len);
    int  (*recv)(void *, uint8_t *buf, size_t cap, size_t *got, uint32_t ms);
    int  (*firmware_read)(void *, const char *name, uint8_t *buf,
                          size_t cap, size_t *len);
    void (*log)(void *, const char *line);
} vqeaf_bt_ops_t;

typedef struct {
    const vqeaf_bt_ops_t *ops;
    int powered;
    int hci_ready;      /* 1 chỉ khi HCI Reset trả về thật */
    int last_error;
} vqeaf_bt_t;

void vqeaf_bt_init(vqeaf_bt_t *bt, const vqeaf_bt_ops_t *ops);
/* 0 chỉ khi HCI sống; mọi lỗi trả errno âm và hci_ready=0. */
int  vqeaf_bt_start(vqeaf_bt_t *bt, const char *fw_name);
void vqeaf_bt_stop(vqeaf_bt_t *bt);
#endif
```

### Bước 3 — Viết driver fail-closed

`kernel/drivers/<subsystem>/<ten>.c`:

- Kiểm đủ callback trước khi chạm phần cứng: thiếu một cái → `-EINVAL`.
- Đọc trước khi ghi; mỗi bước ghi log "sắp làm gì" và "thấy gì".
- Lỗi ở đâu dọn ngược tới đó (tắt nguồn, đóng UART), không để nửa sống.
- Cờ "ready" chỉ bật từ phản hồi thật (ví dụ HCI Reset trả `0x0C 0x00`),
  **không** bật từ timeout, từ sleep cố định hay từ "hàm trả 0".
- Không tự bật phần cứng khi boot: chỉ bật khi có lệnh rõ ràng.

### Bước 4 — Viết backend platform cho violet

`platform/violet/<ten>_bringup.c` (+ `.h`): mount phân vùng read-only, mở
`/dev`/sysfs, đọc file firmware thật, đẩy log qua `vio_log`, và trả về chuỗi
trạng thái cho rescue verb. Ví dụ mẫu đang chạy tốt:
`platform/violet/wifi_bringup.c` (mount p86/p82/p74, `firmware_class.path`,
`/lib/firmware`) và `platform/violet/wifi_fw_check.c` (kiểm container firmware).

Nguyên tắc: tầng này **không** tự quyết định "Wi-Fi/Bluetooth đã lên"; nó chỉ
báo sự thật nó đọc được (`wlan0` có/không, HCI device có/không).

### Bước 5 — Xử lý firmware đúng cách

1. **Không commit blob vào git.** Firmware của hãng lấy từ phân vùng trên
   chính máy (NON-HLOS.bin, p38, p82) hoặc từ bản ROM người dùng tự tải.
2. **Trích xuất có ghi chép:** mọi file stage phải có nguồn + kích thước +
   SHA-256. Mẫu: `firmware/violet/manifest.json`,
   `scripts/wifi/inspect_violet_firmware.py`, `scripts/extract_stock_firmware.sh`.
3. **Stage vào ảnh:** thêm vào `scripts/package_violet.py` (như khối
   `NEWFW` đang copy `wlanmdsp.mbn`, `bdwlan.*`, `regdb.bin` vào
   `/lib/firmware` và ghi `radio-firmware-status.txt`).
4. **Kiểm container trước khi giao cho phần cứng.** Đã có sẵn cho Wi-Fi:
   `vqeaf_wifi_fw_probe()` / `vqeaf_wifi_fw_check_set()` trong
   `kernel/drivers/wifi/wcn3990/wifi_firmware.c` — kiểm ELF, program header,
   `PT_LOAD` nằm trong file, entry nằm trong segment, machine 164 (Hexagon),
   BDF version, regdb magic. Driver mới nên làm tương tự cho định dạng của nó.
5. **Không tự sinh MAC/calibration/khoá.** Thiếu dữ liệu thì báo `BLOCKED`,
   tuyệt đối không tạo giá trị giả.

### Bước 6 — Nối vào kênh chẩn đoán (QEAFRESCUE)

Thêm verb vào `platform/violet/channel/fs/rescue.c` theo mẫu `wifi ...`:

- `<ten> state` — trạng thái một dòng cho status bar.
- `<ten> fw` — danh sách file firmware thật + kết quả kiểm container.
- `<ten> diag` — 5 dòng có bằng chứng (đã kiểm/đã hỏng, kèm số đo).
- `<ten> off` — nhả tài nguyên, dừng retry.

Lý do: trên máy thật, log kernel stock thường không đọc được; QEAFRESCUE
(via `F:\QEAF.CMD` → `RESPONSE.TXT`) là nguồn sự thật duy nhất.

### Bước 7 — Test và cổng kiểm

- **Host test cho driver:** `tests/<subsystem>/test_<ten>.c` với callback giả
  (chỉ để kiểm thứ tự lifecycle/rollback), khẳng định cả nhánh thành công và
  từng nhánh hỏng. Mẫu: `tests/wifi/test_firmware_boot.c`.
- **Test trên dữ liệu thật:** nếu có file firmware thật trong `build/`, cho
  test assert trực tiếp lên số đo (kích thước, số segment, machine…). Mẫu:
  `tests/wifi/test_wifi_firmware.c`.
- **Cổng đọc source:** một test Python khẳng định code không chứa đường
  "thành công giả" (không bật ready từ sleep, không bỏ qua thiếu firmware).
  Mẫu: `tests/wifi/test_firmware_boot.py`.
- **Thêm vào danh sách build:** `scripts/build_violet.py` (`SOURCES`) hoặc
  `CMakeLists.txt` (kernel freestanding).
- Chạy: `python scripts/wifi/run_hal_tests.py`, `python scripts/package_violet.py`.

### Bước 8 — Chứng minh trên máy (không thay bằng build)

```bash
python scripts/device_check.py --flash          # chỉ ghi p77
python scripts/device_check.py --steps "cmd <ten> state; cmd <ten> fw"
```

Đọc `RESPONSE.TXT`/`QEAFBOOT.LOG` trên volume `F:`, đối chiếu với `status`.
Nếu không có phần cứng trong tay: ghi rõ **"chưa chạy trên máy"**, không suy
từ "build PASS" ra "tính năng chạy".

---

## 4. Ví dụ đang có: Wi-Fi WCN3990 (đường đi đầy đủ)

| Tầng | File | Vai trò |
| --- | --- | --- |
| Hợp đồng | `kernel/include/vqeaf/wcn3990.h`, `wifi_device.h`, `wifi_firmware.h`, `firmware_boot.h` | tài nguyên DT, container firmware, state machine |
| Driver | `kernel/drivers/wcn3990_snoc.c`, `kernel/drivers/wifi/wcn3990/*` | probe fail-closed, QRTR/QMI, kiểm firmware |
| Platform | `platform/violet/wifi_bringup.c`, `wifi_fw_check.c` | mount p86/p82/p74, firmware path, báo cáo thật |
| Firmware | `build/violet-fw/image/*` → `/lib/firmware` | `wlanmdsp.mbn`, `bdwlan.*`, `regdb.bin` + SHA-256 |
| Chẩn đoán | `wifi state`, `wifi fw`, `wifi diag`, `wifi discover` trong `rescue.c` | trả lời từ máy |
| Test | `tests/wifi/*` | lifecycle, container thật, cổng chống "thành công giả" |
| Tài liệu | `docs/wifi/*.md` | audit, test report, loader |

Trạng thái thật: firmware đã được kiểm container; **`wlan0` vẫn chưa có** vì
persist thiếu `wlan_mac.bin`/calibration, WLFW `0x45` chưa publish, modem reset
sau ~40 s và kernel tự viết chưa có network stack. Đây là ví dụ về việc tách
bạch: cái gì xong thì nói xong, cái gì chặn thì nêu đúng chặn.

---

## 5. Việc tiếp theo nên làm cho violet (theo thứ tự)

1. **Bluetooth (WCN3990, p38)** — hẹp nhất và có firmware sẵn: mount p38 ro
   (đã có trong `wifi_bringup.c`), mở UART, gửi HCI Reset, chỉ báo ready khi
   nhận `0x0C 0x00`. Cần xác minh chân `BT_EN`, clock và NVM trước.
2. **ADSP/audio** — cần firmware ADSP + mailbox/GLINK; làm sau khi có driver
   kiểu bước-thang như HakiOS (`adsp_step=N`).
3. **Camera, GPU, fingerprint** — chỉ sau khi kernel tự viết có bus/IRQ/DMA
   thật; hiện chưa có nền để không giả.

---

## 6. Checklist trước khi nói "đã xong"

- [ ] Đầu file ghi rõ: đã kiểm gì trên violet, đâu là giả định.
- [ ] Không có địa chỉ/ thông số lấy từ máy khác (SM7325, MT689x…).
- [ ] Thiếu callback/tài nguyên → `-EINVAL`/`-ENODEV`, không chạy tiếp.
- [ ] Trạng thái "ready" chỉ đến từ phản hồi phần cứng thật.
- [ ] Firmware: có nguồn, có SHA-256, không commit blob, có kiểm container.
- [ ] Có verb chẩn đoán trên QEAFRESCUE + log một dòng cho status bar.
- [ ] Host test phủ nhánh hỏng, không chỉ nhánh đẹp.
- [ ] Đã chạy `run_hal_tests.py` + `package_violet.py` (và `ctest` nếu đụng kernel).
- [ ] Nếu chưa chạy trên máy: ghi "chưa chạy trên máy", kèm bước để người dùng kiểm.
