/* SurfaceFlinger-like compositor for the independent AArch64 kernel (F3).
 *
 * Two generations live here on purpose:
 *   - F2c legacy: EL0 clients submit small solid surfaces via
 *     sys_submit_surface into an SPSC ring. Kept as a regression path.
 *   - F3 real layers: each EL0 task owns a window (bounds, z, visibility,
 *     backing buffer mapped into its own user root). The compositor rebuilds
 *     every frame as background -> legacy surfaces -> windows by ascending z,
 *     with clipping, then reads back pixels over UART for the gate.
 *
 * The framebuffer is guest RAM scanned out by QEMU ramfb. EMULATED evidence
 * only (QEMU virt), not physical violet hardware.
 */
#include "vqeaf/compositor.h"
#include "vqeaf/display_aarch64.h"
#include "vqeaf/uart_aarch64.h"
#include "vqeaf/window.h"
#include "vqeaf/arch_aarch64.h"

#define SURF_MAX_PIX 256u
#define SURF_MAX_BYTES (16u + SURF_MAX_PIX * 4u)
#define SQ_SIZE 4u
#define BG 0xFF101010u

struct surface { uint32_t dst_x, dst_y, w, h; uint32_t px[SURF_MAX_PIX]; };
static struct surface sq[SQ_SIZE];
static volatile unsigned sq_head; /* written by the submitter (EL0 syscall) */
static volatile unsigned sq_tail; /* written by the compositor task */
static uint64_t fb_pa;
/* Copies retained so every F3 frame can be rebuilt from scratch. */
static struct surface legacy[SQ_SIZE];
static unsigned legacy_count;

void vqeaf_compositor_init(void)
{
    sq_head = 0; sq_tail = 0; legacy_count = 0;
    fb_pa = vqeaf_display_fb_pa();
}

int vqeaf_compositor_submit(const void *ubuf, uint64_t len)
{
    if(len < 16 || len > SURF_MAX_BYTES) return -2;
    const uint32_t *u = (const uint32_t *)ubuf;
    uint32_t dx = u[0], dy = u[1], w = u[2], h = u[3];
    if(!w || !h || w > VQEAF_FB_WIDTH || h > VQEAF_FB_HEIGHT) return -3;
    if(w * h > SURF_MAX_PIX) return -4;
    if(len < 16 + (uint64_t)w * h * 4) return -5;
    unsigned next = (sq_head + 1) % SQ_SIZE;
    if(next == sq_tail) return -6; /* ring full */
    struct surface *s = &sq[sq_head];
    s->dst_x = dx; s->dst_y = dy; s->w = w; s->h = h;
    for(uint32_t i = 0; i < w * h; i++) s->px[i] = u[4 + i];
    __asm__ volatile("dsb sy" ::: "memory");
    sq_head = next;
    return 0;
}

static void blit(volatile uint32_t *fb, const struct surface *s)
{
    for(uint32_t y = 0; y < s->h; y++)
        for(uint32_t x = 0; x < s->w; x++) {
            uint32_t dx = s->dst_x + x, dy = s->dst_y + y;
            if(dx < VQEAF_FB_WIDTH && dy < VQEAF_FB_HEIGHT)
                fb[dy * VQEAF_FB_WIDTH + dx] = s->px[y * s->w + x];
        }
}

static void rebuild(volatile uint32_t *fb)
{
    for(uint32_t i = 0; i < VQEAF_FB_WIDTH * VQEAF_FB_HEIGHT; i++) fb[i] = BG;
    for(unsigned i = 0; i < legacy_count; i++) blit(fb, &legacy[i]);
    vqeaf_wm_compose(fb);
    __asm__ volatile("dsb sy" ::: "memory");
}

static void delay(volatile uint64_t n) { while(n--) __asm__ volatile("nop"); }

static uint32_t full_checksum(volatile uint32_t *fb)
{
    uint32_t sum = 0;
    for(uint32_t i = 0; i < VQEAF_FB_WIDTH * VQEAF_FB_HEIGHT; i++) sum += fb[i];
    return sum;
}

static void pixel(const char *tag, volatile uint32_t *fb, uint32_t x, uint32_t y)
{
    vqeaf_uart_write(tag);
    vqeaf_uart_hex64(fb[y * VQEAF_FB_WIDTH + x]);
    vqeaf_uart_write("\r\n");
}

void vqeaf_compositor_task(void *arg)
{
    (void)arg;
    if(!fb_pa) { vqeaf_uart_write("COMPOSITOR: no framebuffer\r\n"); return; }
    vqeaf_uart_write("COMPOSITOR: online\r\n");
    volatile uint32_t *fb = (volatile uint32_t *)fb_pa;

    /* ---- Phase 1: F2c regression evidence --------------------------------
     * Wait for the two legacy EL0 surface submissions and blit them. The
     * readback/checksum here are computed while the framebuffer contains ONLY
     * background + those two surfaces, so the old F2c gate stays meaningful. */
    unsigned composed = 0;
    for(int iter = 0; iter < 4000 && composed < 2; iter++) {
        while(sq_tail != sq_head) {
            blit(fb, &sq[sq_tail]);
            if(legacy_count < SQ_SIZE) legacy[legacy_count++] = sq[sq_tail];
            sq_tail = (sq_tail + 1) % SQ_SIZE;
            composed++;
        }
        delay(100000); /* timer IRQ preempts us so clients can submit */
    }
    __asm__ volatile("dsb sy" ::: "memory");
    vqeaf_uart_write("COMPOSITOR: composed ");
    vqeaf_uart_u64(composed);
    vqeaf_uart_write(" surfaces\r\n");
    pixel("COMPOSITOR: pixel(4,4)=", fb, 4, 4);
    pixel("COMPOSITOR: pixel(20,4)=", fb, 20, 4);
    pixel("COMPOSITOR: pixel(40,40)=", fb, 40, 40);
    vqeaf_uart_write("COMPOSITOR: checksum=");
    vqeaf_uart_hex64(full_checksum(fb));
    vqeaf_uart_write("\r\n");
    vqeaf_uart_write("COMPOSITOR: frame ready\r\n");

    /* ---- Phase 2: F3 real window/layer frames ---------------------------
     * Rebuild the whole framebuffer each frame. Evidence is emitted once the
     * SystemUI launcher window and the AMS-spawned app window are both up and
     * at least three keyboard events have been routed by focus. */
    uint32_t frames = 0;
    uint32_t last_gen = 0xFFFFFFFFu;
    int done = 0;
    uint64_t start = vqeaf_a64_timer_ticks();
    /* Damage-driven composition + a hard 10 s (1000 timer ticks) deadline so
     * a missing keyboard event cannot hang QEMU forever. */
    while(!done && vqeaf_a64_timer_ticks() - start < 1000u) {
        uint32_t gen = vqeaf_wm_generation();
        if(gen != last_gen) { rebuild(fb); frames++; last_gen = gen; }
        if(vqeaf_wm_count() >= 2 && vqeaf_wm_deliveries() >= 3) {
            vqeaf_uart_write("COMPOSITOR: f3 frame ");
            vqeaf_uart_u64(frames);
            vqeaf_uart_write(" composed (windows=");
            vqeaf_uart_u64((uint64_t)vqeaf_wm_count());
            vqeaf_uart_write(", focus=");
            vqeaf_uart_u64((uint64_t)(int64_t)vqeaf_wm_focused());
            vqeaf_uart_write(", deliveries=");
            vqeaf_uart_u64(vqeaf_wm_deliveries());
            vqeaf_uart_write(", input_irqs=");
            vqeaf_uart_u64(vqeaf_a64_input_irq_count());
            vqeaf_uart_write(")\r\n");
            /* (70,80) is launcher-only; (100,100) is inside the z=1 app
             * window that overlaps the launcher: proves z order + clipping. */
            pixel("COMPOSITOR: f3 pixel(70,80)=", fb, 70, 80);
            pixel("COMPOSITOR: f3 pixel(100,100)=", fb, 100, 100);
            pixel("COMPOSITOR: f3 pixel(4,4)=", fb, 4, 4);
            pixel("COMPOSITOR: f3 pixel(20,4)=", fb, 20, 4);
            pixel("COMPOSITOR: f3 pixel(300,220)=", fb, 300, 220);
            vqeaf_uart_write("COMPOSITOR: f3 checksum=");
            vqeaf_uart_hex64(full_checksum(fb));
            vqeaf_uart_write("\r\n");
            vqeaf_uart_write("COMPOSITOR: f3 frame ready\r\n");
            vqeaf_wm_set_frame_done();
            done = 1;
        }
        delay(2000); /* yield to the timer IRQ so EL0 clients can run */
    }
    if(!done) {
        vqeaf_uart_write("COMPOSITOR: f3 TIMEOUT (windows=");
        vqeaf_uart_u64((uint64_t)vqeaf_wm_count());
        vqeaf_uart_write(", deliveries=");
        vqeaf_uart_u64(vqeaf_wm_deliveries());
        vqeaf_uart_write(")\r\n");
        vqeaf_wm_set_frame_done(); /* let EL0 tasks exit instead of hanging */
    }
    vqeaf_uart_write("COMPOSITOR: f3 frames composed=");
    vqeaf_uart_u64(frames);
    vqeaf_uart_write("\r\n");
#ifdef VQEAF_COMPOSITOR_HOLD
    /* Diagnostic build only (-DVQEAF_COMPOSITOR_HOLD): keep the composited frame
     * on screen so a QMP screendump can capture it before PSCI power-off. */
    for(volatile uint64_t h = 0; h < 40000000ull; h++) __asm__ volatile("nop");
#endif
}
