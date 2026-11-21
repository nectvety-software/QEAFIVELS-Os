/* QEMU virt display bring-up: fw_cfg MMIO + ramfb standalone framebuffer.
 *
 * fw_cfg lives at 0x09020000 (data @0x00, selector @0x08, DMA @0x10). The ramfb
 * device is configured by DMA-writing a RAMFBCfg blob to the "etc/ramfb" fw_cfg
 * file; QEMU then scans out the guest-RAM framebuffer at the given physical
 * address. The DMA descriptor is big-endian; the RAMFBCfg fields are little-
 * endian. EMULATED evidence (QEMU virt), not physical violet hardware. */
#include "vqeaf/display_aarch64.h"
#include "vqeaf/uart_aarch64.h"
#include "vqeaf/memory.h"

#define FW_CFG_BASE 0x09020000ull
#define FW_CFG_DATA8  (*(volatile uint8_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_DATA32 (*(volatile uint32_t *)(FW_CFG_BASE + 0x00))
#define FW_CFG_SEL    (*(volatile uint16_t *)(FW_CFG_BASE + 0x08))
#define FW_CFG_DMA    (*(volatile uint64_t *)(FW_CFG_BASE + 0x10))

#define FW_CFG_SIGNATURE 0x0000u
#define FW_CFG_FILE_DIR  0x0019u
#define DMA_CTL_ERROR  0x01u
#define DMA_CTL_READ   0x02u
#define DMA_CTL_SELECT 0x08u
#define DMA_CTL_WRITE  0x10u
#define DRM_FORMAT_XRGB8888 0x34325258u

struct __attribute__((packed)) FwDma { uint32_t control; uint32_t length; uint64_t address; };
/* QEMU hw/display/ramfb.c: all fields big-endian, addr first, 28 bytes. */
struct __attribute__((packed)) RamfbCfg {
    uint64_t addr; uint32_t fourcc; uint32_t flags;
    uint32_t width; uint32_t height; uint32_t stride;
};

static uint16_t ramfb_key;
static int ramfb_found;
static uint64_t fb_pa;
static struct RamfbCfg ramfb_cfg; /* static: identity-mapped kernel .bss */

static inline uint32_t be32(uint32_t v) { return __builtin_bswap32(v); }
static inline uint64_t be64(uint64_t v) { return __builtin_bswap64(v); }

static int fw_dma(uint16_t key, uint64_t buf_pa, uint32_t len, uint32_t ctl)
{
    struct FwDma d;
    d.control = be32(ctl | DMA_CTL_SELECT | ((uint32_t)key << 16));
    d.length = be32(len);
    d.address = be64(buf_pa);
    __asm__ volatile("dsb sy" ::: "memory");
    /* The DMA address register is DEVICE_BIG_ENDIAN: write the descriptor PA
     * byte-swapped so QEMU recovers the real address. */
    FW_CFG_DMA = be64((uint64_t)(uintptr_t)&d);
    __asm__ volatile("dsb sy" ::: "memory");
    volatile uint32_t *ctlp = &d.control; /* QEMU writes the result back via DMA */
    for(int i = 0; i < 1000000; i++) {
        uint32_t c = be32(*ctlp);
        if(!(c & (DMA_CTL_READ | DMA_CTL_WRITE | DMA_CTL_SELECT))) {
            if(c & DMA_CTL_ERROR) {
                vqeaf_uart_write("DISPLAY: DMA error ctl=");
                vqeaf_uart_hex64(c);
                vqeaf_uart_write("\r\n");
                return -1;
            }
            return 0;
        }
    }
    vqeaf_uart_write("DISPLAY: DMA timeout ctl=");
    vqeaf_uart_hex64(be32(*ctlp));
    vqeaf_uart_write("\r\n");
    return -2;
}

static int name_eq(const char *a, const char *b)
{
    while(*a && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

int vqeaf_display_init(void)
{
    FW_CFG_SEL = FW_CFG_SIGNATURE;
    uint32_t sig = FW_CFG_DATA32; /* wire bytes "QEMU" read as LE u32 */
    if(sig != 0x554D4551u) {
        vqeaf_uart_write("DISPLAY: fw_cfg signature BAD=");
        vqeaf_uart_hex64(sig);
        vqeaf_uart_write("\r\n");
        return -1;
    }
    vqeaf_uart_write("DISPLAY: fw_cfg signature OK\r\n");

    /* Read the file directory via DMA: the MMIO data register returns 0 for
     * this entry on QEMU 11. dirbuf is static kernel .bss (identity-mapped). */
    static uint8_t dirbuf[4 + 256 * 64];
    int dr = fw_dma(FW_CFG_FILE_DIR, (uint64_t)(uintptr_t)dirbuf, 4, DMA_CTL_READ);
    if(dr != 0) {
        vqeaf_uart_write("DISPLAY: filedir DMA count read FAILED r=");
        vqeaf_uart_u64((uint64_t)(-dr));
        vqeaf_uart_write("\r\n");
        return -2;
    }
    uint32_t count = be32(*(uint32_t *)dirbuf);
    vqeaf_uart_write("DISPLAY: fw_cfg file count=");
    vqeaf_uart_u64(count);
    vqeaf_uart_write("\r\n");
    if(count > 256u) count = 256u;
    uint32_t dirlen = 4 + count * 64;
    if(fw_dma(FW_CFG_FILE_DIR, (uint64_t)(uintptr_t)dirbuf, dirlen, DMA_CTL_READ) != 0) {
        vqeaf_uart_write("DISPLAY: filedir DMA read FAILED\r\n");
        return -2;
    }
    ramfb_found = 0;
    for(uint32_t i = 0; i < count; i++) {
        uint8_t *e = dirbuf + 4 + i * 64; /* size(4) select(2) reserved(2) name(56) */
        uint16_t sel = (uint16_t)(((uint16_t)e[4] << 8) | e[5]);
        if(name_eq((const char *)&e[8], "etc/ramfb")) {
            ramfb_key = sel; ramfb_found = 1;
        }
    }
    if(!ramfb_found) {
        vqeaf_uart_write("DISPLAY: etc/ramfb not found (attach -device ramfb)\r\n");
        return -2;
    }
    vqeaf_uart_write("DISPLAY: ramfb cfg key=");
    vqeaf_uart_hex64(ramfb_key);
    vqeaf_uart_write("\r\n");

    uint64_t pages = (VQEAF_FB_BYTES + VQEAF_PAGE_SIZE - 1) / VQEAF_PAGE_SIZE;
    fb_pa = vqeaf_pmm_alloc_pages(pages);
    if(!fb_pa) { vqeaf_uart_write("DISPLAY: framebuffer alloc FAILED\r\n"); return -3; }
    volatile uint32_t *px = (volatile uint32_t *)fb_pa;
    for(uint32_t i = 0; i < VQEAF_FB_WIDTH * VQEAF_FB_HEIGHT; i++)
        px[i] = 0xFF101010u; /* graphite background */
    __asm__ volatile("dsb sy" ::: "memory");
    vqeaf_uart_write("DISPLAY: framebuffer pa=");
    vqeaf_uart_hex64(fb_pa);
    vqeaf_uart_write("\r\n");
    return 0;
}

int vqeaf_ramfb_set(uint64_t addr, uint32_t width, uint32_t height, uint32_t stride)
{
    if(!ramfb_found) return -1;
    ramfb_cfg.addr = be64(addr);
    ramfb_cfg.fourcc = be32(DRM_FORMAT_XRGB8888);
    ramfb_cfg.flags = 0;
    ramfb_cfg.width = be32(width);
    ramfb_cfg.height = be32(height);
    ramfb_cfg.stride = be32(stride);
    __asm__ volatile("dsb sy" ::: "memory");
    int r = fw_dma(ramfb_key, (uint64_t)(uintptr_t)&ramfb_cfg,
                   (uint32_t)sizeof(ramfb_cfg), DMA_CTL_WRITE);
    if(r != 0) {
        vqeaf_uart_write("DISPLAY: ramfb DMA write FAILED r=");
        vqeaf_uart_u64((uint64_t)(-r));
        vqeaf_uart_write("\r\n");
        return r;
    }
    vqeaf_uart_write("DISPLAY: ramfb programmed ");
    vqeaf_uart_u64(width);
    vqeaf_uart_write("x");
    vqeaf_uart_u64(height);
    vqeaf_uart_write("\r\n");
    return 0;
}

uint64_t vqeaf_display_fb_pa(void) { return fb_pa; }