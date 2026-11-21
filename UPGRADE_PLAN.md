# Kế hoạch nâng cấp dự án VQEAF OS hiện tại bằng cách tham khảo QeafdropOs

**Dự án hiện tại:** `D:\hdhQEAFIVELS` — VQEAF OS, x86_64 UEFI reference, QEMU, phiên bản 0.2.0  
**Dự án tham khảo:** `D:\QeafivelsOfficial\QeafdropOs` — QeafdropOs / VQEAF OS, ARM64 aarch64-musl, Redmi Note 7 Pro (violet), phiên bản 0.1.0  
**Mục tiêu:** Áp dụng các mẫu thiết kế, quy trình và cấu trúc từ QeafdropOs vào dự án x86_64 UEFI hiện tại để nâng cấp độ chín của OS, không phải port sang ARM64.

---

## 1. Tổng quan sự khác biệt (không thể sao chép trực tiếp)

| Mục | QeafdropOs (violet) | Dự án hiện tại (x86_64 UEFI) | Có thể áp dụng? |
|-----|---------------------|------------------------------|-----------------|
| Kiến trúc | ARM64, `homo-init` PID 1, initramfs trên kernel stock | x86_64, UEFI Boot Manager → KERNEL.EFI → boot protocol v3 | Không (khác ISA) |
| Panel | DRM/KMS dumb buffer, `/dev/dri/card0` | GOP framebuffer qua UEFI | Không (khác API phần cứng) |
| USB gadget | configfs + `f_mass_storage`, LUN QEAFRESCUE | Không có USB gadget driver | Không (x86 UEFI QEMU) |
| Kênh lưu | FAT16 LUN 32 MiB RAM (`/run/qeaf-lun.img`) | FAT16 qua VirtIO/AHCI + VFS | Có (cấu trúc thư mục) |
| Kênh lệnh | `QEAF.CMD` / `RESPONSE.TXT` qua FAT16 LUN | Không có | Có (phát triển thêm) |
| Phiên bản | `qeaf_version.h` (single source of truth) | Phiên bản trong `CMakeLists.txt` | Có |
| Xác thực ảnh | `validate_bootimg.py`, audit kênh cứu hộ | Chưa có | Có |
| Định dạng app | `.qeapp` (manifest + src + assets) | Shell 8 app, chưa đóng gói | Có (hướng tới) |
| Giao diện | `atlas.json` + sprite thủ tục | Vẽ thủ tục C, chưa có spec | Có |

---

## 2. Các mẫu sẽ áp dụng (theo mức độ ưu tiên)

### 2.1. Single source of truth cho phiên bản OS
**Nguồn:** `D:\QeafivelsOfficial\QeafdropOs\qeaf_version.h`  
**Hành động:** Tạo `D:\hdhQEAFIVELS\system\core\include\vqeaf\version.h` (đã tạo).  
Tất cả màn hình khởi động, recovery, USB descriptor, DEVINFO.TXT, nhật ký build sẽ lấy tên/phiên bản từ macro này thay vì hardcode Chuỗi `"VQEAF OS 0.2.0"` khắp nơi.

### 2.2. Cấu trúc thư mục lưu trữ chuẩn
**Nguồn:** `fs/storage.c` của QeafdropOs — năm thư mục gốc `VQEAF`, `UserData`, `Install` + nested `system/`, `apps/`, `framework/`, `themes/`, `boot/`, `drivers/`, `services/`, `updates/`, `logs/`, `cache/`.  
**Hành động:** Định nghĩa lại cấu trúc ổ lưu FAT16 trong dự án x86_64 (kernel/storage + VFS) để các ứng dụng và hệ thống biết nơi đụng tới dữ liệu. Tham khảo `PROMPT.md` của QeafdropOs (mục SDCARD layout).

### 2.3. Kênh lệnh qua tệp trên ổ lưu (Rescue channel)
**Nguồn:** `fs/rescue.c` của QeafdropOs — `QEAF.CMD` được ghi từ máy chủ, OS phát hiện, chạy các động từ (`status`, `echo`, `usb`, `sync`, `rotate`, `flash`, `reboot`...), ghi `RESPONSE.TXT` phản hồi.  
**Hành động:** Thiết kế kênh gỡ lỗi tương tự cho x86_64 UEFI: trên ổ FAT16 VirtIO/AHCI, OS quét tệp lệnh (ví dụ `VQEAF.CMD`), thực thi các động từ an toàn (không ghi phân vùng trên x86 reference), ghi phản hồi ra `RESPONSE.TXT`. Điều này cho phép máy chủ điều khiển/gỡ lỗi OS qua serial hoặc ổ lưu mà không cần gõ phím trên QEMU.

### 2.4. Xác thực ảnh boot trước khi ghi
**Nguồn:** `scripts/validate_bootimg.py` của QeafdropOs — kiểm tra cấu trúc boot image (page_size, kernel gzip, ramdisk, địa chỉ, kích thước) trước khi cho phép ghi.  
**Hành động:** Viết `scripts/validate_bootimg.py` cho dự án x86_64 để xác thực `dist/vqeaf-x86_64-uefi.img` và `dist/vqeaf-x86_64-recovery.img` trước khi viết vào ổ thử nghiệm QEMU. Đây là layer phòng thủ tránh ghi ảnh hỏng làm hỏng phân vùng boot.

### 2.5. Atlas giao diện khởi động (procedural sprites)
**Nguồn:** `atlas.json` của QeafdropOs — định nghĩa canvas, palette, sprite thủ tục (logo, vòng tròn, sóng, biểu tượng...), màn hình fastboot/recovery, dialog xác nhận. Không có asset nhị phân, toàn bộ vẽ bằng số nguyên.  
**Hành động:** Tạo `atlas.json` cho dự án x86_64 (đã có vẽ thủ tục C) để làm đặc tả giao diện khởi động và recovery. Các màn hình hiện tại (boot splash, recovery menu) sẽ được ánh xạ vào spec này.

### 2.6. Định dạng ứng dụng `.qeapp`
**Nguồn:** QeafdropOs — ứng dụng đóng gói thành `.qeapp`: `manifest.json` + `src/*.c` (hàm `lv_obj_t *create(lv_obj_t*)` + `q_create`) + `assets/icon.png` + `README.md`.  
**Hành động:** Thiết kế định dạng đóng gói ứng dụng đơn giản cho x86_64 UEFI (không dùng LVGL, dùng gfx của dự án), cho phép thêm ứng dụng mà không phải sửa lại kernel. Đây là hướng dài hạn, không phải phần bắt buộc của nâng cấp này.

### 2.7. Quy tắc an toàn trước khi ghi phân vùng
**Nguồn:** QeafdropOs — chỉ ghi p77 (recovery), phải có token `confirm`, đọc lại từng byte verify.  
**Hành động:** Thêm guard tương tự cho scripts ghi ảnh vào ổ QEMU: yêu cầu cờ `--commit`, từ chối ghi nếu không có `--commit`, in cảnh báo rõ ràng.

---

## 3. Lộ trình thực hiện (4 giai đoạn)

| Giai đoạn | Công việc | File liên quan |
|---------|-----------|----------------|
| **Giai đoạn 1 (này)** | Tạo `version.h` đơn nguồn | `system/core/include/vqeaf/version.h` |
| **Giai đoạn 1** | Viết kế hoạch này | `UPGRADE_PLAN.md` |
| **Giai đoạn 1** | Tạo `atlas.json` giao diện khởi động | `atlas.json` |
| **Giai đoạn 2** | Viết `scripts/validate_bootimg.py` | `scripts/validate_bootimg.py` |
| **Giai đoạn 2** | Cập nhật `CMakeLists.txt` để tích hợp `version.h` | `CMakeLists.txt` |
| **Giai đoạn 3** | Thiết kế cấu trúc thư mục ổ lưu (FAT16) | `system/core/include/vqeaf/storage.h` (kể từ hiện có), `kernel/core/fs_fat16.c` |
| **Giai đoạn 3** | Thêm kênh lệnh `VQEAF.CMD` / `RESPONSE.TXT` | `system/core/`, `scripts/` |
| **Giai đoạn 4** | Thiết kế định dạng đóng gói app (`.qeapp` tương đương) | `scripts/make_app.py` (tương lai) |

---

## 4. Ràng buộc và cảnh báo

- **Không port code ARM64 sang x86_64.** Driver DRM/KMS, USB gadget configfs, panel violet không áp dụng được cho x86_64 UEFI/QEMU. Chỉ lấy mẫu thiết kế (thiết kế giao diện, cấu trúc phiên bản, quy trình xác thực).
- **Không ghi vào thiết bị vật lý.** Dự án x86_64 hiện tại chỉ chạy trong QEMU. Bất kỳ script ghi ảnh nào cũng phải chỉ tác động vào tệp ảo/VirtIO, không bao giờ ghi phân vùng thực của máy tính chủ.
- **Phiên bản duy nhất.** Sau khi thêm `version.h`, mọi chỗ hardcode `"VQEAF OS 0.2.0"` trong C hoặc tài liệu phải được đưa về macro `VQEAF_OS_STRING`.

---

## 5. Tài liệu tham khảo trong QeafdropOs

- `qeaf_version.h` — đơn nguồn phiên bản
- `fs/rescue.c` — kênh lệnh qua FAT16
- `fs/storage.c` — cấu trúc thư mục lưu trữ
- `scripts/validate_bootimg.py` — xác thực ảnh boot
- `scripts/mk_bootimg_violet.py` — đóng gói ảnh boot (tham khảo cấu trúc header)
- `atlas.json` — spec giao diện khởi động thủ tục
- `PROMPT.md` — quy tắc và layout SDCARD/LUN
- `ARCHITECTURE.md` — kiến trúc nguồn chuẩn
