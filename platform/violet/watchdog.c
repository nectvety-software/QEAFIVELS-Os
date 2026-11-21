/* SPDX-License-Identifier: MIT */
/* platform/violet/watchdog.c - feed the hardware watchdog while the OS is
 * actually running. See watchdog.h for why this exists. */
#include "watchdog.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "violet.h"

#define WD_TIMEOUT_S 30
#define WD_PET_MS     5000
#define WD_STALE_MS   8000

static int wd_fd = -1;
static uint64_t wd_last_pet;
static volatile uint64_t wd_last_ack;

int vio_watchdog_start(void)
{
    static const char *const paths[] = {"/dev/watchdog", "/dev/watchdog0"};
    int saved = ENODEV;
    int timeout = WD_TIMEOUT_S;
    size_t i;

    for(i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        wd_fd = open(paths[i], O_WRONLY | O_CLOEXEC);
        if(wd_fd >= 0) {
            if(ioctl(wd_fd, WDIOC_SETTIMEOUT, &timeout) == 0)
                vio_log("watchdog: %s armed, timeout=%ds", paths[i], timeout);
            else
                vio_log("watchdog: %s opened, timeout unchanged (%s)",
                        paths[i], strerror(errno));
            wd_last_ack = wd_last_pet = vio_monotonic_ms();
            return 0;
        }
        saved = errno;
    }
    /* No driver behind the node: say so once and leave the loop alone. */
    vio_log("watchdog: no watchdog device (%s); the SoC timer stays unmanaged",
            strerror(saved));
    return -1;
}

void vio_watchdog_ack(void)
{
    wd_last_ack = vio_monotonic_ms();
}

void vio_watchdog_pet(void)
{
    uint64_t now;

    if(wd_fd < 0) return;
    now = vio_monotonic_ms();
    /* Main loop wedged: stop feeding, let the timer do its job. */
    if(now - wd_last_ack > WD_STALE_MS) return;
    if(now - wd_last_pet < WD_PET_MS) return;
    if(ioctl(wd_fd, WDIOC_KEEPALIVE, 0) == 0)
        wd_last_pet = now;
    else
        vio_log("watchdog: keepalive failed (%s)", strerror(errno));
}
