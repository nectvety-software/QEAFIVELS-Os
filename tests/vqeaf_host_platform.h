/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_host_platform.h - host stand-ins for violet platform helpers.
 *
 * The real vqeaf_state.c reads sysfs; both host harnesses (render_phone_ui
 * and vqeaf_shell_interaction) replace it with fixed values. These four
 * symbols are the remaining platform calls the shell links against. Each
 * harness includes this header from exactly one translation unit, so the
 * definitions stay single-copy per binary.
 */
#ifndef VQEAF_HOST_PLATFORM_H
#define VQEAF_HOST_PLATFORM_H

#include <stdint.h>

#include "lvgl.h"
#include "vqeaf_state.h"

int vqeaf_state_set_volume(int percent)
{
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;
    vqeaf_state.volume = percent;
    return 0;
}

int vqeaf_state_set_screen_timeout(int seconds)
{
    if(seconds < 0) seconds = 0;
    vqeaf_state.screen_timeout_s = seconds;
    return 0;
}

/* On the device this is CLOCK_MONOTONIC in ms; LVGL's host tick is the
 * equivalent fake clock for the shell's idle-timeout logic. */
uint64_t vio_monotonic_ms(void)
{
    return (uint64_t)lv_tick_get();
}

int vio_set_backlight(int percent)
{
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;
    vqeaf_state.brightness = percent;
    return 0;
}

/* The device writes to /dev/kmsg; the host harness discards log lines. */
void vio_log(const char *fmt, ...)
{
    (void)fmt;
}

void vio_sleep_ms(unsigned ms)
{
    (void)ms;
}

#endif /* VQEAF_HOST_PLATFORM_H */
