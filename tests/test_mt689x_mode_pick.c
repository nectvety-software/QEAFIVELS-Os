/* SPDX-License-Identifier: MIT */
/* tests/test_mt689x_mode_pick.c - host check of the DRM mode choice rule.
 *
 * platform/violet/drm_mode_pick.h is the exact code drm_kms.c runs. The
 * chopin panel exposes 30/48/50/60/120 Hz for the same 1080x2400 size, so
 * this proves the PREFERRED flag (120 Hz) wins instead of the first entry.
 * Build/run: scripts/test_mt689x_mode_pick.py.
 */
#include <stdio.h>
#include <string.h>

#include "drm_mode_pick.h"

static int failures;

static struct drm_mode_modeinfo mode(int w, int h, int type)
{
    struct drm_mode_modeinfo m;

    memset(&m, 0, sizeof(m));
    m.hdisplay = (uint16_t)w;
    m.vdisplay = (uint16_t)h;
    m.type = (uint32_t)type;
    return m;
}

static void expect_index(const char *what, int got, int want)
{
    if(got != want) {
        printf("FAIL %s: got %d, want %d\n", what, got, want);
        failures++;
    }
}

int main(void)
{
    /* chopin: same size, five timings, 120 Hz preferred. */
    struct drm_mode_modeinfo chopin[5];

    chopin[0] = mode(1080, 2400, DRM_MODE_TYPE_DRIVER);
    chopin[1] = mode(1080, 2400, DRM_MODE_TYPE_DRIVER);
    chopin[2] = mode(1080, 2400, DRM_MODE_TYPE_DRIVER);
    chopin[3] = mode(1080, 2400, DRM_MODE_TYPE_DRIVER);
    chopin[4] = mode(1080, 2400,
                     DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED);
    expect_index("chopin prefers 120 Hz among same-size modes",
                 vio_pick_mode_index(chopin, 5, NULL, 1080, 2400), 4);

    /* Same list with no preferred flag: first exact-size entry. */
    struct drm_mode_modeinfo plain[5];

    for(int i = 0; i < 5; i++)
        plain[i] = mode(1080, 2400, DRM_MODE_TYPE_DRIVER);
    expect_index("no preferred -> first exact size",
                 vio_pick_mode_index(plain, 5, NULL, 1080, 2400), 0);

    /* violet: one 1080x2340 mode marked preferred. */
    struct drm_mode_modeinfo violet[1];

    violet[0] = mode(1080, 2340,
                     DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED);
    expect_index("violet single mode",
                 vio_pick_mode_index(violet, 1, NULL, 1080, 2340), 0);

    /* HDMI-style list where the panel size is not present: preferred wins. */
    struct drm_mode_modeinfo other[3];

    other[0] = mode(1280, 720, DRM_MODE_TYPE_DRIVER);
    other[1] = mode(1920, 1080,
                    DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED);
    other[2] = mode(1024, 768, DRM_MODE_TYPE_DRIVER);
    expect_index("size absent -> preferred",
                 vio_pick_mode_index(other, 3, NULL, 1080, 2400), 1);

    /* Size absent, no preferred, current mode present: stay on current. */
    struct drm_mode_modeinfo plain_other[3];

    plain_other[0] = mode(1280, 720, DRM_MODE_TYPE_DRIVER);
    plain_other[1] = mode(1024, 768, DRM_MODE_TYPE_DRIVER);
    plain_other[2] = mode(640, 480, DRM_MODE_TYPE_DRIVER);
    struct drm_mode_modeinfo current = mode(1024, 768, DRM_MODE_TYPE_DRIVER);

    expect_index("size absent, current listed",
                 vio_pick_mode_index(plain_other, 3, &current, 1080, 2400), 1);

    /* Nothing matches and nothing is preferred: largest area. */
    expect_index("fallback largest area",
                 vio_pick_mode_index(plain_other, 3, NULL, 999, 999), 0);

    /* Empty list is a refusal, not a guess. */
    expect_index("empty list refused", vio_pick_mode_index(NULL, 0, NULL,
                                                          1080, 2400), -1);

    if(failures == 0)
        printf("test_mt689x_mode_pick: OK (120 Hz preferred, violet unchanged)\n");
    return failures ? 1 : 0;
}
