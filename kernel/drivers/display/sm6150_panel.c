#include <stddef.h>
#include "vqeaf/sm6150_panel.h"

static const vqeaf_sm6150_panel_mode_t reference_mode = {
    1080u, 90u, 120u, 2u, 1292u,
    2340u, 13u, 8u, 3u, 2364u,
    60u, 183257280ull, VQEAF_SM6150_DSI_TRAFFIC_BURST
};

const vqeaf_sm6150_panel_mode_t *vqeaf_sm6150_panel_reference_mode(void)
{
    return &reference_mode;
}

int vqeaf_sm6150_panel_reset(void *context, vqeaf_sm6150_gpio_set_fn set_gpio,
                             vqeaf_sm6150_delay_us_fn delay_us)
{
    if (set_gpio == NULL || delay_us == NULL) return -1;
    if (set_gpio(context, 1) != 0) return -2;
    delay_us(context, 1000u);
    if (set_gpio(context, 0) != 0) return -3;
    delay_us(context, 1000u);
    if (set_gpio(context, 1) != 0) return -4;
    delay_us(context, 10000u);
    return 0;
}
