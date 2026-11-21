#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../system/ui/include/vqeaf/gfx.h"
#include "../system/ui/include/vqeaf/recovery_screen.h"
#include "../system/ui/include/vqeaf/screen_common.h"
#include "../system/ui/include/vqeaf/update_screen.h"

#define BIG_W 1280u
#define BIG_H 800u
static uint32_t small_px[240u * 320u];
static uint32_t big_px[BIG_W * BIG_H];

static uint32_t accent_pixels(const uint32_t *px, uint32_t count) {
    uint32_t teal = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t c = px[i];
        uint32_t r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
        if (g > 120u && b > 100u && r < 120u) teal++;
    }
    return teal;
}

static void dump_ppm(const char *path, const uint32_t *px, uint32_t w, uint32_t h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) {
        uint32_t c = px[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 16) & 255u),
                                (unsigned char)((c >> 8) & 255u),
                                (unsigned char)(c & 255u)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(void) {
    VqeafFramebuffer small, big;
    assert(vqeaf_gfx_setup(&small, (uint64_t)(uintptr_t)small_px, 240, 320, 240,
                           VQEAF_GFX_FMT_BGR) == 0);
    assert(vqeaf_gfx_setup(&big, (uint64_t)(uintptr_t)big_px, BIG_W, BIG_H, BIG_W,
                           VQEAF_GFX_FMT_BGR) == 0);

    assert(vqeaf_recovery_item_available(VQEAF_RECOVERY_REBOOT_SYSTEM) == 1);
    assert(vqeaf_recovery_item_available(VQEAF_RECOVERY_APPLY_UPDATE) == 1);
    assert(vqeaf_recovery_item_available(VQEAF_RECOVERY_WIPE_DATA) == 0);
    assert(vqeaf_recovery_item_available(VQEAF_RECOVERY_POWER_OFF) == 1);
    assert(vqeaf_fastboot_item_available(VQEAF_FASTBOOT_CONTINUE) == 1);
    assert(vqeaf_fastboot_item_available(VQEAF_FASTBOOT_RECOVERY) == 0);
    assert(vqeaf_update_stage_label(0) && vqeaf_update_stage_label(3));
    assert(vqeaf_update_stage_label(4) == 0);

    memset(small_px, 0, sizeof small_px);
    vqeaf_recovery_screen_render(&small, 1, 0);
    assert(accent_pixels(small_px, 240u * 320u) > 250u);

    memset(big_px, 0, sizeof big_px);
    vqeaf_recovery_screen_render(&big, 0, 0);
    uint32_t teal = accent_pixels(big_px, BIG_W * BIG_H);
    assert(teal > 3000u);
    dump_ppm("screens/recovery_ui.ppm", big_px, BIG_W, BIG_H);

    memset(big_px, 0, sizeof big_px);
    VqeafDeviceInfo info = {"VQEAF Reference", "x86_64 UEFI", "v0.3.0", "reference",
                            "QEMU x86_64"};
    vqeaf_fastboot_screen_render(&big, &info, 2, 0);
    assert(accent_pixels(big_px, BIG_W * BIG_H) > 2000u);
    dump_ppm("screens/fastboot_ui.ppm", big_px, BIG_W, BIG_H);

    memset(big_px, 0, sizeof big_px);
    VqeafUpdateView blocked = {0, 0, 0, 0, 0};
    vqeaf_update_screen_render_view(&big, &blocked);
    assert(accent_pixels(big_px, BIG_W * BIG_H) > 1500u);
    dump_ppm("screens/update_blocked.ppm", big_px, BIG_W, BIG_H);

    memset(big_px, 0, sizeof big_px);
    VqeafUpdateView progress = {62, 2, "Kiem tra pin va dung luong", 0, 1};
    vqeaf_update_screen_render_view(&big, &progress);
    assert(accent_pixels(big_px, BIG_W * BIG_H) > 2000u);

    memset(big_px, 0, sizeof big_px);
    VqeafUpdateView failed = {40, 2, 0, 1, 1};
    vqeaf_update_screen_render_view(&big, &failed);
    uint32_t red = 0;
    for (uint32_t i = 0; i < BIG_W * BIG_H; i++) {
        uint32_t c = big_px[i];
        if (((c >> 16) & 255u) > 180u && ((c >> 8) & 255u) < 120u) red++;
    }
    assert(red > 300u);
    return 0;
}
