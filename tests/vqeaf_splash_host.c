/* Host render harness for the independent kernel's violet boot splash.
 *
 * Calls the real platform/violet/splash.c renderer with the real boot lines
 * used by main_violet.c and writes a PPM. scripts/render_boot_splash.py turns
 * that into a PNG so the terminal layout can be inspected without hardware.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../platform/violet/splash.h"

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/screens/boot-terminal.ppm";
    int w = argc > 2 ? atoi(argv[2]) : 720;
    int h = argc > 3 ? atoi(argv[3]) : 1560;
    if (w <= 0 || h <= 0) return 2;
    uint32_t *buf = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!buf) return 3;

    static const char *const lines[] = {
        "[  OK  ] drm: scanout ready",
        "[  OK  ] lun: QEAFRESCUE bound",
        "[  OK  ] rescue: QEAF.CMD online",
        NULL,
    };
    vio_splash_render(buf, w, h, "QEAFIVELS OS", "0.2.0 violet", lines);

    FILE *f = fopen(out, "wb");
    if (!f) { free(buf); return 4; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint32_t p = buf[i];
        unsigned char rgb[3] = { (unsigned char)((p >> 16) & 0xFFu),
                                 (unsigned char)((p >> 8) & 0xFFu),
                                 (unsigned char)(p & 0xFFu) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    free(buf);
    return 0;
}
