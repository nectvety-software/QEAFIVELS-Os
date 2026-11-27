#ifndef VQEAF_SM6150_PANEL_H
#define VQEAF_SM6150_PANEL_H
#include <stdint.h>

#define VQEAF_SM6150_DSI_TRAFFIC_SYNC_PULSE 0u
#define VQEAF_SM6150_DSI_TRAFFIC_SYNC_EVENT 1u
#define VQEAF_SM6150_DSI_TRAFFIC_BURST 2u

typedef struct {
    uint32_t h_active, h_front, h_back, h_pulse, h_total;
    uint32_t v_active, v_front, v_back, v_pulse, v_total;
    uint32_t refresh_hz;
    uint64_t pixel_clock_hz;
    uint32_t dsi_traffic_mode;
} vqeaf_sm6150_panel_mode_t;

/* Reference mode for novatek,nt36672a-shenchao. The ABL DTB does not carry
 * timing or initialization commands, so this is not proof of the fitted panel. */
const vqeaf_sm6150_panel_mode_t *vqeaf_sm6150_panel_reference_mode(void);

typedef int (*vqeaf_sm6150_gpio_set_fn)(void *context, int high);
typedef void (*vqeaf_sm6150_delay_us_fn)(void *context, uint32_t microseconds);
int vqeaf_sm6150_panel_reset(void *context, vqeaf_sm6150_gpio_set_fn set_gpio,
                             vqeaf_sm6150_delay_us_fn delay_us);
#endif
