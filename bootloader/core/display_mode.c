#include "display_mode.h"
#include "../../kernel/include/vqeaf/serial.h"

#define VBE_INDEX_ID 0x0u
#define VBE_INDEX_XRES 0x1u
#define VBE_INDEX_YRES 0x2u
#define VBE_INDEX_BPP 0x3u
#define VBE_INDEX_ENABLE 0x4u
#define VBE_INDEX_VIRT_WIDTH 0x6u
#define VBE_INDEX_VIRT_HEIGHT 0x7u
#define VBE_DISABLED 0x00u
#define VBE_ENABLED 0x01u
#define VBE_LFB_ENABLED 0x40u
#define VBE_ID0 0xB0C0u
#define VBE_ID5 0xB0C6u
#define VBE_PORT_INDEX 0x1CEu
#define VBE_PORT_DATA 0x1CFu
#define PORTRAIT_W 720u
#define PORTRAIT_H 1280u

static void outw16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0,%1" ::"a"(value), "Nd"(port));
}
static uint16_t inw16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}
static uint16_t dispi_read(uint16_t index) {
    outw16(VBE_PORT_INDEX, index);
    return inw16(VBE_PORT_DATA);
}
static void dispi_write(uint16_t index, uint16_t value) {
    outw16(VBE_PORT_INDEX, index);
    outw16(VBE_PORT_DATA, value);
}
static void serial_hex(uint64_t v) {
    static const char h[] = "0123456789ABCDEF";
    vqeaf_serial_write("0x");
    for (int s = 60; s >= 0; s -= 4) vqeaf_serial_char(h[(v >> s) & 15]);
}
static int exact_9x16(uint32_t w, uint32_t h) {
    return w < h && (uint64_t)w * 16u == (uint64_t)h * 9u;
}

/* Program the Bochs VBE interface used by QEMU's std VGA. Ports 0x1CE/0x1CF
 * exist only on that family, so the ID is checked before any write and all
 * geometry is read back. Returns 0 only when the requested mode is live. */
static int vbe_portrait_mode(void) {
    uint16_t id = dispi_read(VBE_INDEX_ID);
    vqeaf_serial_write("DISPLAY: VBE id=");
    serial_hex(id);
    vqeaf_serial_write("\r\n");
    if (id < VBE_ID0 || id > VBE_ID5) return -1;
    /* Keep the firmware's mode so a rejected request can be undone instead of
     * leaving a Bochs-compatible adapter in a disabled state. */
    uint16_t old_x = dispi_read(VBE_INDEX_XRES);
    uint16_t old_y = dispi_read(VBE_INDEX_YRES);
    uint16_t old_bpp = dispi_read(VBE_INDEX_BPP);
    uint16_t old_vw = dispi_read(VBE_INDEX_VIRT_WIDTH);
    uint16_t old_vh = dispi_read(VBE_INDEX_VIRT_HEIGHT);
    uint16_t old_enable = dispi_read(VBE_INDEX_ENABLE);
    dispi_write(VBE_INDEX_ENABLE, VBE_DISABLED);
    dispi_write(VBE_INDEX_XRES, (uint16_t)PORTRAIT_W);
    dispi_write(VBE_INDEX_YRES, (uint16_t)PORTRAIT_H);
    dispi_write(VBE_INDEX_BPP, 32u);
    dispi_write(VBE_INDEX_VIRT_WIDTH, (uint16_t)PORTRAIT_W);
    dispi_write(VBE_INDEX_VIRT_HEIGHT, (uint16_t)PORTRAIT_H);
    dispi_write(VBE_INDEX_ENABLE, VBE_ENABLED | VBE_LFB_ENABLED);
    if (dispi_read(VBE_INDEX_XRES) != PORTRAIT_W ||
        dispi_read(VBE_INDEX_YRES) != PORTRAIT_H ||
        dispi_read(VBE_INDEX_BPP) != 32u ||
        !(dispi_read(VBE_INDEX_ENABLE) & VBE_ENABLED)) {
        vqeaf_serial_write("DISPLAY: VBE rejected the portrait mode\r\n");
        dispi_write(VBE_INDEX_ENABLE, VBE_DISABLED);
        dispi_write(VBE_INDEX_XRES, old_x);
        dispi_write(VBE_INDEX_YRES, old_y);
        dispi_write(VBE_INDEX_BPP, old_bpp);
        dispi_write(VBE_INDEX_VIRT_WIDTH, old_vw);
        dispi_write(VBE_INDEX_VIRT_HEIGHT, old_vh);
        dispi_write(VBE_INDEX_ENABLE, old_enable & (VBE_ENABLED | VBE_LFB_ENABLED));
        return -1;
    }
    return 0;
}

int vqeaf_display_choose_portrait(EFI_SYSTEM_TABLE *st,
                                  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
                                  VqeafDisplayChoice *out) {
    if (!out) return -1;
    out->base = out->bytes = 0;
    out->width = out->height = out->stride = 0;
    out->format = VQEAF_DISPLAY_BGR;
    out->portrait = 0;
    out->custom = 0;
    if (!gop || !gop->Mode || !gop->Mode->Info || !gop->Mode->FrameBufferBase) {
        vqeaf_serial_write("DISPLAY: no GOP linear framebuffer\r\n");
        return -1;
    }

    /* 1. An exact 9:16 portrait mode already advertised by firmware. */
    int best = -1, best_rank = 99;
    if (gop->Mode->MaxMode && gop->QueryMode && gop->SetMode) {
        for (UINT32 i = 0; i < gop->Mode->MaxMode; i++) {
            UINTN size = 0;
            EFI_GOP_MODE_INFO *info = 0;
            if (gop->QueryMode(gop, i, &size, &info) != 0 || !info) continue;
            uint32_t w = info->HorizontalResolution, h = info->VerticalResolution;
            int usable = info->PixelFormat <= EFI_GOP_PIXEL_BGR;
            int rank = 99;
            if (usable && w == 1080u && h == 1920u) rank = 0;
            else if (usable && w == PORTRAIT_W && h == PORTRAIT_H) rank = 1;
            else if (usable && exact_9x16(w, h)) rank = 2;
            if (rank < 99) {
                vqeaf_serial_write("GOP: mode=");
                serial_hex(i);
                vqeaf_serial_write(" portrait 9:16 ");
                serial_hex(w);
                vqeaf_serial_char('x');
                serial_hex(h);
                vqeaf_serial_write("\r\n");
            }
            if (rank < best_rank) {
                best_rank = rank;
                best = (int)i;
            }
            ((EFI_FREE_POOL)BS(st)->FreePool)(info);
        }
    }
    if (best >= 0 && (UINT32)best != gop->Mode->Mode) {
        if (gop->SetMode(gop, (UINT32)best) != 0) best = -1;
    }
    if (best >= 0) {
        EFI_GOP_MODE_INFO *mi = gop->Mode->Info;
        uint64_t base = gop->Mode->FrameBufferBase;
        uint64_t bytes = gop->Mode->FrameBufferSize;
        if (!bytes) bytes = (uint64_t)mi->HorizontalResolution * mi->VerticalResolution * 4u;
        if (!exact_9x16(mi->HorizontalResolution, mi->VerticalResolution) || !base) {
            vqeaf_serial_write("DISPLAY: GOP portrait mode did not take effect\r\n");
        } else {
            out->base = base;
            out->bytes = bytes;
            out->width = mi->HorizontalResolution;
            out->height = mi->VerticalResolution;
            out->stride = mi->PixelsPerScanLine;
            out->format = mi->PixelFormat == EFI_GOP_PIXEL_RGB ? VQEAF_DISPLAY_RGB
                                                               : VQEAF_DISPLAY_BGR;
            out->portrait = 1;
            vqeaf_serial_write("DISPLAY: portrait 9:16 GOP ");
            serial_hex(out->width);
            vqeaf_serial_char('x');
            serial_hex(out->height);
            vqeaf_serial_write("\r\n");
            return 0;
        }
    }

    /* 2. No portrait GOP mode: drive the QEMU linear framebuffer directly.
     * OVMF only advertises landscape VBE modes on this device, but the VGA
     * hardware itself accepts 720x1280x32 with the same LFB base. */
    if (vbe_portrait_mode() == 0) {
        out->base = gop->Mode->FrameBufferBase;
        out->bytes = (uint64_t)PORTRAIT_W * PORTRAIT_H * 4u;
        out->width = PORTRAIT_W;
        out->height = PORTRAIT_H;
        out->stride = PORTRAIT_W;
        out->format = VQEAF_DISPLAY_BGR; /* X8R8G8B8 little-endian */
        out->portrait = 1;
        out->custom = 1;
        vqeaf_serial_write("DISPLAY: portrait 9:16 VBE ");
        serial_hex(out->width);
        vqeaf_serial_char('x');
        serial_hex(out->height);
        vqeaf_serial_write("\r\n");
        return 0;
    }

    /* 3. Landscape fallback: keep the firmware mode; the shell centres a
     * 9:16 column inside it instead of stretching. */
    {
        EFI_GOP_MODE_INFO *mi = gop->Mode->Info;
        out->base = gop->Mode->FrameBufferBase;
        out->bytes = gop->Mode->FrameBufferSize;
        if (!out->bytes)
            out->bytes = (uint64_t)mi->HorizontalResolution * mi->VerticalResolution * 4u;
        out->width = mi->HorizontalResolution;
        out->height = mi->VerticalResolution;
        out->stride = mi->PixelsPerScanLine;
        out->format = mi->PixelFormat == EFI_GOP_PIXEL_RGB ? VQEAF_DISPLAY_RGB
                                                           : VQEAF_DISPLAY_BGR;
        vqeaf_serial_write("DISPLAY: no 9:16 mode; using ");
        serial_hex(out->width);
        vqeaf_serial_char('x');
        serial_hex(out->height);
        vqeaf_serial_write(" with a centred shell column\r\n");
    }
    return 0;
}
