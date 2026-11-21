/* VQEAF Recovery 0.3: standalone UEFI recovery/fastboot-style environment.
 *
 * Renders the shared recovery screen on GOP, navigates it with the firmware
 * keyboard (arrow keys + Enter), and only performs actions the platform can
 * actually honor:
 *   - Reboot System  -> 8042 reset pulse (0x64/0xFE)
 *   - Apply Update   -> blocked installer view; there is no updater service
 *   - Power Off      -> ACPI PM1a_CNT soft-off (q35 0x604, older 0xB004)
 *   - Wipe/Advanced  -> drawn disabled, confirm reports "not supported"
 * No storage is written. If GOP is unavailable the previous text-only report
 * is printed instead of pretending a menu exists.
 */
#include "../../bootloader/core/efi_minimal.h"
#include "../../bootloader/core/display_mode.h"
#include "../../kernel/include/vqeaf/serial.h"
#include "../../system/ui/include/vqeaf/gfx.h"
#include "../../system/ui/include/vqeaf/recovery_screen.h"
#include "../../system/ui/include/vqeaf/update_screen.h"

static CHAR16 title[] = L"VQEAF Recovery 0.3\r\n";
static CHAR16 no_gop[] = L"No GOP framebuffer: recovery menu unavailable.\r\n";
static CHAR16 l1[] = L"Standalone recovery environment\r\n";
static CHAR16 l2[] = L"Reboot / power-off are wired to firmware.\r\n";
static CHAR16 l3[] = L"Update installer: BLOCKED (no updater service).\r\n";
static CHAR16 l4[] = L"Wipe data/cache: NOT IMPLEMENTED.\r\n";

/* EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID: 9042A9DE-23DC-4A38-96FB-7ADED080516A */
static EFI_GUID gop_guid = {{0xde, 0xa9, 0x42, 0x90, 0xdc, 0x23, 0x38, 0x4a,
                             0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};

static void serial_hex(uint64_t v) {
    static const char h[] = "0123456789ABCDEF";
    vqeaf_serial_write("0x");
    for (int s = 60; s >= 0; s -= 4) vqeaf_serial_char(h[(v >> s) & 15]);
}
static void out8(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port));
}
static void out16(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(port));
}
static void stall(EFI_SYSTEM_TABLE *st, UINTN us) {
    EFI_STALL s = (EFI_STALL)BS(st)->Stall;
    if (s) s(us);
}

static int gop_init(EFI_SYSTEM_TABLE *st, VqeafFramebuffer *fb) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS s = ((EFI_LOCATE_PROTOCOL)BS(st)->LocateProtocol)(&gop_guid, 0, (void **)&gop);
    if (EFI_ERROR(s) || !gop)
        s = ((EFI_HANDLE_PROTOCOL)BS(st)->HandleProtocol)(st->ConsoleOutHandle,
                                                          &gop_guid, (void **)&gop);
    if (EFI_ERROR(s) || !gop || !gop->Mode || !gop->Mode->Info) return -1;
    VqeafDisplayChoice display;
    if (vqeaf_display_choose_portrait(st, gop, &display) != 0 || !display.base ||
        !display.bytes ||
        vqeaf_gfx_setup(fb, display.base, display.width, display.height,
                        display.stride, display.format) != 0)
        return -1;
    vqeaf_serial_write("RECOVERY: GOP mode=");
    serial_hex(display.width);
    vqeaf_serial_char('x');
    serial_hex(display.height);
    vqeaf_serial_write(display.portrait ? " portrait base=" : " base=");
    serial_hex(display.base);
    vqeaf_serial_write("\r\n");
    return 0;
}

static void render_menu(VqeafFramebuffer *fb, uint32_t selected, const char *status) {
    vqeaf_recovery_screen_render(fb, selected, status);
}

static void render_blocked_update(VqeafFramebuffer *fb) {
    VqeafUpdateView view;
    view.percent = 0;
    view.stage = 0;
    view.detail = "Kh\u00F4ng th\u1EC3 c\u1EADp nh\u1EADt trong Recovery";
    view.failed = 0;
    view.available = 0;
    vqeaf_update_screen_render_view(fb, &view);
}

static void machine_reboot(void) {
    vqeaf_serial_write("RECOVERY: action=reboot via 8042\r\n");
    out8(0x64, 0xFE);
    for (;;) __asm__ volatile("cli; hlt");
}
static void machine_power_off(void) {
    vqeaf_serial_write("RECOVERY: action=poweroff via ACPI PM1a_CNT\r\n");
    out16(0x604, 0x2000);  /* QEMU q35 PM1a control: SLP_EN | S5 */
    out16(0xB004, 0x2000); /* older chipset fallback */
    out16(0x4004, 0x3400);
    for (;;) __asm__ volatile("cli; hlt");
}

static void show_status(VqeafFramebuffer *fb, uint32_t selected, const char *msg) {
    render_menu(fb, selected, msg);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    (void)image;
    vqeaf_serial_init();
    vqeaf_serial_write("VQEAF Recovery 0.3\r\n");
    st->ConOut->OutputString(st->ConOut, title);

    VqeafFramebuffer fb;
    if (gop_init(st, &fb) != 0) {
        st->ConOut->OutputString(st->ConOut, no_gop);
        st->ConOut->OutputString(st->ConOut, l1);
        st->ConOut->OutputString(st->ConOut, l2);
        st->ConOut->OutputString(st->ConOut, l3);
        st->ConOut->OutputString(st->ConOut, l4);
        vqeaf_serial_write("RECOVERY: no GOP, text report only\r\n");
        return EFI_SUCCESS;
    }

    uint32_t selected = 0;
    int update_open = 0;
    char status[72];
    status[0] = 0;
    render_menu(&fb, selected, status);
    vqeaf_serial_write("RECOVERY: menu ready (arrows + Enter)\r\n");

    for (;;) {
        if (update_open) {
            EFI_INPUT_KEY k;
            render_blocked_update(&fb);
            vqeaf_serial_write("RECOVERY: update installer blocked (no service)\r\n");
            do {
                stall(st, 15000);
            } while (!st->ConIn || EFI_ERROR(st->ConIn->ReadKey(st->ConIn, &k)));
            update_open = 0;
            status[0] = 0;
            render_menu(&fb, selected, status);
            continue;
        }
        EFI_INPUT_KEY key;
        if (!st->ConIn || EFI_ERROR(st->ConIn->ReadKey(st->ConIn, &key))) {
            stall(st, 15000);
            continue;
        }
        if (key.ScanCode == EFI_SCAN_UP) {
            selected = (selected + VQEAF_RECOVERY_ITEM_COUNT - 1u) % VQEAF_RECOVERY_ITEM_COUNT;
            status[0] = 0;
        } else if (key.ScanCode == EFI_SCAN_DOWN) {
            selected = (selected + 1u) % VQEAF_RECOVERY_ITEM_COUNT;
            status[0] = 0;
        } else if (key.UnicodeChar == 0x0D) {
            if (!vqeaf_recovery_item_available(selected)) {
                show_status(&fb, selected, "M\u1EE5c n\u00E0y ch\u01B0a h\u1ED7 tr\u1EE3");
                vqeaf_serial_write("RECOVERY: disabled item ignored\r\n");
                continue;
            }
            if (selected == VQEAF_RECOVERY_REBOOT_SYSTEM) {
                show_status(&fb, selected, "\u0110ang kh\u1EDFi \u0111\u1ED9ng l\u1EA1i...");
                stall(st, 250000);
                machine_reboot();
            } else if (selected == VQEAF_RECOVERY_APPLY_UPDATE) {
                vqeaf_serial_write("RECOVERY: action=apply-update -> blocked view\r\n");
                update_open = 1;
                continue;
            } else if (selected == VQEAF_RECOVERY_POWER_OFF) {
                show_status(&fb, selected, "\u0110ang t\u1EAFt ngu\u1ED3n...");
                stall(st, 250000);
                machine_power_off();
            }
        } else if (key.UnicodeChar == 0x1B) {
            status[0] = 0;
        }
        render_menu(&fb, selected, status);
    }
}
