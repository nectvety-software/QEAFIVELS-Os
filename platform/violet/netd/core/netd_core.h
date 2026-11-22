/* SPDX-License-Identifier: MIT */
#ifndef QEAF_NETD_CORE_H
#define QEAF_NETD_CORE_H
#include <stdint.h>
#include <stddef.h>

typedef enum {
    NETD_VERIFIED = 0,
    NETD_FAILED,
    NETD_BLOCKED,
    NETD_UNKNOWN
} netd_state_t;

const char *netd_state_name(netd_state_t s);
/* Formats "net: <state> <evidence>" into dst. Returns bytes used or -1. */
int netd_log_line(char *dst, size_t cap, netd_state_t state,
                  const char *evidence);
/* Monotonic milliseconds, independent of RTC/NTP. */
uint64_t netd_now_ms(void);

/* Minimal non-blocking event loop. Callbacks never block the main loop. */
typedef void (*netd_timer_fn)(void *ctx);
typedef struct {
    int epfd;
    int stop;
} netd_loop_t;

int  netd_loop_init(netd_loop_t *loop);
void netd_loop_close(netd_loop_t *loop);
int  netd_loop_add_fd(netd_loop_t *loop, int fd, void *ctx);
int  netd_loop_add_timer(netd_loop_t *loop, uint32_t period_ms,
                         netd_timer_fn fn, void *ctx);
int  netd_loop_run(netd_loop_t *loop);   /* returns when stop is set */
void netd_loop_stop(netd_loop_t *loop);

#endif
