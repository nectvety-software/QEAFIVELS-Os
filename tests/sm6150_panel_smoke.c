#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "vqeaf/sm6150_panel.h"

typedef struct {
    uint32_t writes;
    int levels[3];
    uint32_t delays[3];
    int fail_at;
} panel_fake_t;

static int set_gpio(void *context, int high)
{
    panel_fake_t *fake = (panel_fake_t *)context;
    if (fake->writes == (uint32_t)fake->fail_at) return -1;
    fake->levels[fake->writes] = high;
    fake->writes++;
    return 0;
}

static void delay_us(void *context, uint32_t microseconds)
{
    panel_fake_t *fake = (panel_fake_t *)context;
    if (fake->writes != 0u) fake->delays[fake->writes - 1u] = microseconds;
}

int main(void)
{
    const vqeaf_sm6150_panel_mode_t *mode = vqeaf_sm6150_panel_reference_mode();
    panel_fake_t fake = {0};
    fake.fail_at = -1;
    assert(mode != NULL);
    assert(mode->h_total == 1292u && mode->v_total == 2364u);
    assert(mode->pixel_clock_hz ==
           (uint64_t)mode->h_total * mode->v_total * mode->refresh_hz);
    assert(mode->dsi_traffic_mode == VQEAF_SM6150_DSI_TRAFFIC_BURST);
    assert(vqeaf_sm6150_panel_reset(&fake, set_gpio, delay_us) == 0);
    assert(fake.writes == 3u);
    assert(fake.levels[0] == 1 && fake.levels[1] == 0 && fake.levels[2] == 1);
    assert(fake.delays[0] == 1000u && fake.delays[1] == 1000u &&
           fake.delays[2] == 10000u);
    fake.writes = 0u;
    fake.fail_at = 1;
    assert(vqeaf_sm6150_panel_reset(&fake, set_gpio, delay_us) == -3);
    assert(fake.writes == 1u);
    assert(vqeaf_sm6150_panel_reset(&fake, NULL, delay_us) == -1);
    puts("SM6150 panel reference/reset smoke: PASS");
    return 0;
}
