/* VQEAF .qen app that opens a real window (F3), AArch64 position-independent.
 *
 * AMS spawns this module as a fresh isolated EL0 process when the SystemUI
 * launcher calls sys_launch. It creates a z=1 layer overlapping the launcher,
 * draws into the kernel-mapped backing buffer, presents it, then receives the
 * keyboard event that InputFlinger routes to the newly focused top layer.
 *
 * g_msg is a global rodata pointer: under -fPIC it carries an
 * R_AARCH64_RELATIVE fixup that vqeaf_qen_load() must apply by the load base,
 * so this module also keeps the F2a relocation proof alive. */
#include "vqeaf_el0.h"

#define APP_X 96u
#define APP_Y 80u
#define APP_W 128u
#define APP_H 96u
#define APP_BUF_VA 0x10030000000ull
#define C_BODY 0xFF202A40u
#define C_TITLE 0xFF35E0C0u
#define C_ICON 0xFFFFFFFFu

const char *volatile g_msg = "APP WINDOW: layer up\r\n";

static volatile u32 *g_fb;
static u32 g_win;
static char g_key_msg[24];

static void rect(u32 x0, u32 y0, u32 w, u32 h, u32 c)
{
    for(u32 y = 0; y < h; y++)
        for(u32 x = 0; x < w; x++)
            g_fb[(y0 + y) * APP_W + x0 + x] = c;
}

static void set_key_msg(u32 k)
{
    const char *p = "APP WINDOW: key=Z\r\n";
    for(u32 i = 0; i < 19; i++) g_key_msg[i] = p[i];
    g_key_msg[16] = (char)k;
    g_key_msg[19] = 0;
}

void *create(const void *api)
{
    (void)api;
    /* F4: real EL0 -> EL1 denial proof. AMS gave this process uid 10002 and a
     * UI-only capability bundle, so the dispatcher must reject both file
     * syscalls with -EACCES before it ever validates an argument pointer. */
    {
        const char *private_path = "/data/10002/private";
        i64 h = vqeaf_sys2(VQEAF_SYS_OPEN,
                           (i64)(u64)(unsigned long)private_path, VQEAF_O_RDONLY);
        el0_print(h == VQEAF_EACCES ? "SEC EL0: sys_open denied -EACCES\r\n"
                                    : "SEC EL0: sys_open NOT denied\r\n");
        i64 w = vqeaf_sys3(VQEAF_SYS_WRITE, 0, 0, 0);
        el0_print(w == VQEAF_EACCES ? "SEC EL0: sys_write denied -EACCES\r\n"
                                    : "SEC EL0: sys_write NOT denied\r\n");
    }

    vqeaf_window_desc_t d;
    d.x = APP_X; d.y = APP_Y; d.w = APP_W; d.h = APP_H;
    d.z = 1; d.flags = VQEAF_WINDOW_FLAG_VISIBLE;
    d.buffer_va = APP_BUF_VA;

    i64 r = vqeaf_sys2(VQEAF_SYS_WINDOW_CREATE,
                       (i64)(u64)(unsigned long)&d, VQEAF_WINDOW_DESC_SIZE);
    if(r <= 0) {
        el0_print("APP WINDOW: create FAILED\r\n");
        el0_exit();
    }
    g_win = (u32)r;
    g_fb = (volatile u32 *)APP_BUF_VA;
    rect(0, 0, APP_W, APP_H, C_BODY);
    rect(0, 0, APP_W, 10, C_TITLE);
    rect(12, 24, 32, 32, C_ICON);
    rect(56, 24, 32, 32, C_ICON);
    vqeaf_sys1(VQEAF_SYS_WINDOW_PRESENT, g_win);
    el0_print((const char *)g_msg);

    for(u64 i = 0; i < 2000000ull; i++) {
        vqeaf_key_event_t ev;
        i64 got = vqeaf_sys2(VQEAF_SYS_POLL_INPUT, g_win,
                             (i64)(u64)(unsigned long)&ev);
        if(got == 1) {
            set_key_msg(ev.key);
            el0_print(g_key_msg);
        }
        if(el0_frame_done()) break;
    }
    vqeaf_sys1(VQEAF_SYS_WINDOW_CLOSE, g_win);
    el0_print("APP WINDOW: closing\r\n");
    el0_exit();
    return 0;
}
