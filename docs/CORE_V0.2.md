# VQEAF Core v0.2 — nền tảng trước giao diện smartphone

## Driver model
Driver descriptor ABI v1 có probe/remove, phân loại thiết bị và trạng thái instance. Registry có giới hạn 32 driver/instance. Probe failure được lưu thành FAILED và trả lỗi, không giả lập thiết bị.

## Device capability registry
Registry tối đa 64 capability, trạng thái UNSUPPORTED/PRESENT/READY/DEGRADED/BLOCKED_FIRMWARE/ERROR. Dịch vụ/UI chỉ bật tính năng khi capability READY. Chưa có driver phần cứng điện thoại được khẳng định là hỗ trợ.

## Storage/VFS
Mount namespace tối đa 8 mount, chọn mount prefix dài nhất, kiểm tra absolute path và traversal. Vnode read/write tiếp tục dùng VFS kernel ABI. FAT16 implementation đã được liên kết vào KERNEL.EFI; hiện chưa có physical block-device driver, GPT/partition discovery trên thiết bị thật, exFAT/F2FS hoặc persistent media để mount.

## Power manager
Screen timeout đúng 15 giây, 1 phút, 5 phút, 30 phút, Never; mặc định 1 phút. Hoạt động người dùng reset idle timer. Bộ đếm phải dùng monotonic milliseconds. Khi display capability không có hoặc hệ thống suspended, manager không giả vờ tắt màn hình. Core v0.2 chưa có display driver/backlight, wake lock, call/emergency policy hay PMIC integration; chỉ quản lý state/policy.

## Ngôn ngữ và font mặc định
Locale mặc định vi-VN, UTF-8, fallback en-US; Ubuntu regular/bold được bundle tại assets/fonts. Giữ LICENCE.txt nguyên bản. Font lấy từ Google Fonts repository google/fonts/ufl/ubuntu, giấy phép Ubuntu Font Licence 1.0. Không đổi tên font hoặc sửa file font. Chuỗi kiểm thử: “Tiếng Việt có dấu: ă â đ ê ô ơ ư; Á Ắ Ằ Ặ ấ ầ ẩ ẫ ậ”.

## Kiểm thử
Host tests kiểm tra driver register/bind/unbind, capability readiness, mount resolution/traversal boundary, timeout policy/activity/suspend, locale defaults. QEMU x86_64 vẫn là reference boot; không có phần cứng điện thoại thật được thử nghiệm.

 
## Kernel integration (2026-11-10)
- Kernel startup now initializes the driver registry, capability registry, block registry, storage mount namespace and screen power manager.
- Kernel links the driver, capability, storage/VFS, block, FAT16 and power modules directly into KERNEL.EFI; these services are no longer host-test-only.
- Capabilities explicitly distinguish working software services from unavailable hardware: GOP framebuffer and screen-timeout policy can be READY; backlight and physical block storage remain UNSUPPORTED until real drivers are bound.
- Screen timeout defaults to 1 minute and is driven by PIT-derived monotonic milliseconds from the existing UI refresh task (no extra scheduler task). Serial keyboard input resets idle time; sleep/blank state wakes on the next input event. Blank-state changes are applied on the next UI refresh to avoid clearing a frame mid-render. The framebuffer is blanked in software; no backlight rail is claimed.
- Boot Manager now interprets splash_init() success correctly, draws only when GOP is valid, and skips framebuffer drawing in serial-only mode. Splash is a short progress phase before StartImage, not a permanent boot state.
- Ubuntu-Regular.ttf is converted offline to a compact fixed-cell glyph table for ASCII and Vietnamese codepoints; kernel and bootloader use UTF-8 decoding and the generated Ubuntu bitmap without runtime font parsing or heap allocations.
- Verification remains QEMU x86_64 + host tests; physical phone drivers and real storage media are not yet available.
