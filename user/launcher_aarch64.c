/* VQEAF SystemUI-like launcher, running at EL0 on the independent AArch64
 * kernel (F3). No libc, no kernel pointers: the launcher owns a window whose
 * backing buffer the kernel mapped into this process's own user root.
 *
 * Chain:
 *   InputFlinger  -> PL011 RX IRQ -> focused window queue -> sys_poll_input
 *   SystemUI      -> this launcher redraws its layer on focus change
 *   AMS           -> sys_launch spawns an isolated EL0 .qen process
 *   SurfaceFlinger-> compositor rebuilds the framebuffer by z order
 *
 * EMULATED QEMU virt evidence only; this is not a violet hardware driver. */
#include "vqeaf_el0.h"

#define WIN_X 48u
#define WIN_Y 40u
#define WIN_W 200u
#define WIN_H 160u
#define BUF_VA 0x10020000000ull

#define C_SURFACE 0xFF0C191Bu
#define C_BAR     0xFF00C8C8u
#define C_FOCUS   0xFFFFFFFFu

static const u32 TILE[6] = {
    0xFF35E0C0u, 0xFF37C8E8u, 0xFF54D6A0u,
    0xFFE8B84Bu, 0xFFE86C6Cu, 0xFF9C7BE8u
};
static const char PKG[] = "com.vqeaf.window";

static volatile u32 *g_fb;
static u32 g_win;
static u32 g_focus;
static char g_focus_msg[24];
static char g_key_msg[24];

static void rect(u32 x0, u32 y0, u32 w, u32 h, u32 c)
{
    for(u32 y = 0; y < h; y++)
        for(u32 x = 0; x < w; x++)
            g_fb[(y0 + y) * WIN_W + x0 + x] = c;
}

static void draw(void)
{
    rect(0, 0, WIN_W, WIN_H, C_SURFACE);
    rect(0, 0, WIN_W, 12, C_BAR);             /* status bar */
    for(u32 i = 0; i < 6; i++) {
        u32 tx = 12 + (i % 3) * 60;
        u32 ty = 24 + (i / 3) * 56;
        rect(tx, ty, 52, 48, TILE[i]);
        if(i == g_focus) {
            rect(tx, ty, 52, 2, C_FOCUS);
            rect(tx, ty + 46, 52, 2, C_FOCUS);
            rect(tx, ty, 2, 48, C_FOCUS);
            rect(tx + 50, ty, 2, 48, C_FOCUS);
        }
    }
}

static void set_focus_msg(u32 f)
{
    const char *p = "LAUNCHER EL0: focus=0\r\n";
    for(u32 i = 0; i < 23; i++) g_focus_msg[i] = p[i];
    g_focus_msg[20] = (char)('0' + f);
    g_focus_msg[23] = 0;
}

static void set_key_msg(u32 k)
{
    const char *p = "LAUNCHER EL0: key=Z\r\n";
    for(u32 i = 0; i < 21; i++) g_key_msg[i] = p[i];
    g_key_msg[18] = (char)k;
    g_key_msg[21] = 0;
}

void _start(void)
{
    vqeaf_window_desc_t d;
    d.x = WIN_X; d.y = WIN_Y; d.w = WIN_W; d.h = WIN_H;
    d.z = 0; d.flags = VQEAF_WINDOW_FLAG_VISIBLE;
    d.buffer_va = BUF_VA;

    i64 r = vqeaf_sys2(VQEAF_SYS_WINDOW_CREATE, (i64)(u64)(unsigned long)&d,
                       VQEAF_WINDOW_DESC_SIZE);
    if(r <= 0) {
        el0_print("LAUNCHER EL0: window create FAILED\r\n");
        el0_exit();
    }
    g_win = (u32)r;
    g_fb = (volatile u32 *)BUF_VA;
    draw();
    vqeaf_sys1(VQEAF_SYS_WINDOW_PRESENT, g_win);
    el0_print("LAUNCHER EL0: home ready\r\n");

    int launched = 0;
    for(u64 i = 0; i < 2000000ull; i++) {
        vqeaf_key_event_t ev;
        i64 got = vqeaf_sys2(VQEAF_SYS_POLL_INPUT, g_win,
                             (i64)(u64)(unsigned long)&ev);
        if(got == 1) {
            u32 k = ev.key;
            set_key_msg(k);
            el0_print(g_key_msg);
            if(k == 'd' || k == 'D' || k == 'a' || k == 'A') {
                g_focus = (g_focus + ((k == 'd' || k == 'D') ? 1u : 5u)) % 6u;
                draw();
                vqeaf_sys1(VQEAF_SYS_WINDOW_PRESENT, g_win);
                set_focus_msg(g_focus);
                el0_print(g_focus_msg);
            } else if((k == '\n' || k == '\r' || k == ' ') && !launched) {
                launched = 1;
                vqeaf_sys1(VQEAF_SYS_LAUNCH,
                           (i64)(u64)(unsigned long)PKG);
                el0_print("LAUNCHER EL0: launch requested\r\n");
            }
        }
        if(el0_frame_done()) break;
    }
    el0_print("LAUNCHER EL0: done\r\n");
    el0_exit();
}
