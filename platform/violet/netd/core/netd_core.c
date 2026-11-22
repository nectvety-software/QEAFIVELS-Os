/* SPDX-License-Identifier: MIT */
/* core: event loop, monotonic timer and the honest net: log format. */
#define _GNU_SOURCE
#include "netd_core.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "../../violet.h"

#define NETD_MAX_FDS 32
#define NETD_MAX_TIMERS 8

typedef struct {
    int fd;
    void *ctx;
} fd_slot_t;

typedef struct {
    int fd;
    uint32_t period_ms;
    netd_timer_fn fn;
    void *ctx;
} timer_slot_t;

static fd_slot_t g_fds[NETD_MAX_FDS];
static int g_fd_count;
static timer_slot_t g_timers[NETD_MAX_TIMERS];
static int g_timer_count;

const char *netd_state_name(netd_state_t s)
{
    switch(s) {
    case NETD_VERIFIED: return "verified";
    case NETD_FAILED:   return "failed";
    case NETD_BLOCKED:  return "blocked";
    default:            return "unknown";
    }
}

int netd_log_line(char *dst, size_t cap, netd_state_t state,
                  const char *evidence)
{
    int n;
    if(dst == NULL || cap < 16) return -1;
    n = snprintf(dst, cap, "net: %s %s",
                 netd_state_name(state), evidence ? evidence : "");
    if(n < 0 || (size_t)n >= cap) return -1;
    return n;
}

uint64_t netd_now_ms(void) { return vio_monotonic_ms(); }

int netd_loop_init(netd_loop_t *loop)
{
    if(loop == NULL) return -1;
    memset(loop, 0, sizeof(*loop));
    loop->epfd = epoll_create1(EPOLL_CLOEXEC);
    if(loop->epfd < 0) return -errno;
    g_fd_count = 0;
    g_timer_count = 0;
    return 0;
}

void netd_loop_close(netd_loop_t *loop)
{
    if(loop == NULL) return;
    for(int i = 0; i < g_timer_count; i++)
        if(g_timers[i].fd >= 0) close(g_timers[i].fd);
    if(loop->epfd >= 0) close(loop->epfd);
    loop->epfd = -1;
}

int netd_loop_add_fd(netd_loop_t *loop, int fd, void *ctx)
{
    struct epoll_event ev;
    if(loop == NULL || fd < 0 || g_fd_count >= NETD_MAX_FDS) return -1;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = ctx;
    if(epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) return -errno;
    g_fds[g_fd_count].fd = fd;
    g_fds[g_fd_count].ctx = ctx;
    g_fd_count++;
    return 0;
}

int netd_loop_add_timer(netd_loop_t *loop, uint32_t period_ms,
                        netd_timer_fn fn, void *ctx)
{
    struct itimerspec its;
    struct epoll_event ev;
    int fd;
    if(loop == NULL || fn == NULL || period_ms == 0 ||
       g_timer_count >= NETD_MAX_TIMERS) return -1;
    fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if(fd < 0) return -errno;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec = period_ms / 1000u;
    its.it_value.tv_nsec = (long)(period_ms % 1000u) * 1000000L;
    its.it_interval = its.it_value;
    if(timerfd_settime(fd, 0, &its, NULL) < 0) {
        close(fd);
        return -errno;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = &g_timers[g_timer_count];
    if(epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        close(fd);
        return -errno;
    }
    g_timers[g_timer_count].fd = fd;
    g_timers[g_timer_count].period_ms = period_ms;
    g_timers[g_timer_count].fn = fn;
    g_timers[g_timer_count].ctx = ctx;
    g_timer_count++;
    return 0;
}

int netd_loop_run(netd_loop_t *loop)
{
    struct epoll_event events[NETD_MAX_FDS + NETD_MAX_TIMERS];
    if(loop == NULL || loop->epfd < 0) return -1;
    while(!loop->stop) {
        int n = epoll_wait(loop->epfd, events, (int)(sizeof(events)/sizeof(events[0])), 1000);
        if(n < 0) {
            if(errno == EINTR) continue;
            return -errno;
        }
        for(int i = 0; i < n; i++) {
            void *ptr = events[i].data.ptr;
            /* Timer slots are registered as &g_timers[idx]; fd slots use ctx.
             * Identify timers by fd, never by pointer arithmetic on user ctx. */
            for(int t = 0; t < g_timer_count; t++) {
                if(&g_timers[t] == ptr) {
                    uint64_t exp;
                    ssize_t r = read(g_timers[t].fd, &exp, sizeof(exp));
                    if(r == (ssize_t)sizeof(exp)) g_timers[t].fn(g_timers[t].ctx);
                    break;
                }
            }
        }
    }
    return 0;
}

void netd_loop_stop(netd_loop_t *loop)
{
    if(loop != NULL) loop->stop = 1;
}
