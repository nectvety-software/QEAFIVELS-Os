/* SPDX-License-Identifier: MIT */
/* platform/violet/violet_tap.h - synthetic tap for the rescue channel.
 *
 * The rescue worker runs on its own thread, the LVGL shell on the main thread.
 * A host `tap X Y` only writes one atomic word here; the UI thread turns it
 * into a press on one poll and a release on the next, so LVGL sees exactly the
 * click a finger would produce at the same canvas point.
 *
 * Packed as (x << 12 | y) + 1, so 0 means "empty" while (0,0) stays a real
 * point, and coordinates fit 0..4095 (the canvas is 720x1560 or 1560x720).
 * One slot only: a second tap queued before the UI consumes the first is
 * refused, never merged.
 */
#ifndef VQEAF_VIOLET_TAP_H
#define VQEAF_VIOLET_TAP_H

#include <stddef.h>
#include <time.h>

#define VQEAF_TAP_COORD_MAX 0xFFFu

#define VQEAF_HOLD_MS_MIN 100
#define VQEAF_HOLD_MS_MAX 10000

#define VQEAF_SWIPE_STEPS_MIN 2
#define VQEAF_SWIPE_STEPS_MAX 64

typedef struct {
    unsigned request;            /* tap: x<<12|y + 1, 0 = empty */
    unsigned long long swipe_request; /* drag, 0 = empty; UI consumes it */
    int release_pending;         /* UI thread only */
    int hold_ticks;              /* extra polls to keep the press down */
    int hold_x, hold_y;          /* point repeated while held */
    /* `hold` verb: a press kept down for a wall-clock duration, so a long-press
     * handler fires exactly as it would under a finger. The duration is stored
     * before the request word, so a reader that wins the exchange sees it. */
    unsigned hold_request;       /* x<<12|y + 1, 0 = empty */
    unsigned hold_ms;            /* duration of the pending hold */
    unsigned long long hold_until; /* UI thread only: monotonic deadline */
} vio_tap_state_t;

static inline unsigned long long vio_tap_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull +
           (unsigned long long)(ts.tv_nsec / 1000000);
}

/* A drag is packed into one 64-bit word so the UI thread never has to read
 * half-written fields: x0 | y0<<12 | x1<<24 | y1<<36 | (steps-1)<<48, plus 1
 * so that a (0,0)->(0,0) drag still means "queued". */
static inline int vio_swipe_queue(vio_tap_state_t *tap, int x0, int y0,
                                  int x1, int y1, int steps)
{
    unsigned long long packed;
    unsigned long long expected = 0ull;

    if(tap == NULL || steps < VQEAF_SWIPE_STEPS_MIN ||
       steps > VQEAF_SWIPE_STEPS_MAX)
        return -1;
    if(x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 ||
       x0 > (int)VQEAF_TAP_COORD_MAX || y0 > (int)VQEAF_TAP_COORD_MAX ||
       x1 > (int)VQEAF_TAP_COORD_MAX || y1 > (int)VQEAF_TAP_COORD_MAX)
        return -1;
    packed = ((unsigned long long)(steps - 1) << 48) |
             ((unsigned long long)y1 << 36) |
             ((unsigned long long)x1 << 24) |
             ((unsigned long long)y0 << 12) |
             (unsigned long long)x0;
    packed += 1ull;
#if defined(__GNUC__) || defined(__clang__)
    if(__atomic_compare_exchange_n(&tap->swipe_request, &expected, packed, 0,
                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        return 0;
#else
    if(tap->swipe_request == 0ull) {
        tap->swipe_request = packed;
        return 0;
    }
#endif
    return -1;
}

/* Returns 1 and fills the drag when one was queued, 0 otherwise. */
static inline int vio_swipe_take(vio_tap_state_t *tap, int *x0, int *y0,
                                 int *x1, int *y1, int *steps)
{
    unsigned long long packed;

    if(tap == NULL) return 0;
#if defined(__GNUC__) || defined(__clang__)
    packed = __atomic_exchange_n(&tap->swipe_request, 0ull, __ATOMIC_SEQ_CST);
#else
    packed = tap->swipe_request;
    tap->swipe_request = 0ull;
#endif
    if(packed == 0ull) return 0;
    packed -= 1ull;
    if(x0) *x0 = (int)(packed & 0xFFFull);
    if(y0) *y0 = (int)((packed >> 12) & 0xFFFull);
    if(x1) *x1 = (int)((packed >> 24) & 0xFFFull);
    if(y1) *y1 = (int)((packed >> 36) & 0xFFFull);
    if(steps) *steps = (int)((packed >> 48) & 0xFFull) + 1;
    return 1;
}

static inline unsigned vio_tap_pack(int x, int y)
{
    return ((unsigned)x << 12) | (unsigned)y;
}

static inline int vio_tap_x(unsigned packed)
{
    return (int)((packed >> 12) & VQEAF_TAP_COORD_MAX);
}

static inline int vio_tap_y(unsigned packed)
{
    return (int)(packed & VQEAF_TAP_COORD_MAX);
}

/* Returns 0 when queued, -1 for an invalid point or when a tap is pending. */
static inline int vio_tap_queue(vio_tap_state_t *tap, int x, int y)
{
    unsigned expected = 0u;
    unsigned packed;

    if(tap == NULL || x < 0 || y < 0 ||
       x > (int)VQEAF_TAP_COORD_MAX || y > (int)VQEAF_TAP_COORD_MAX)
        return -1;
    packed = vio_tap_pack(x, y) + 1u;   /* 0 stays reserved for "empty" */
#if defined(__GNUC__) || defined(__clang__)
    if(__atomic_compare_exchange_n(&tap->request, &expected, packed, 0,
                                   __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        return 0;
#else
    if(tap->request == 0u) {
        tap->request = packed;
        return 0;
    }
#endif
    return -1;
}

/* Returns 0 when queued, -1 for an invalid point/duration or when another
 * synthetic contact (tap, hold or drag) is still pending. */
static inline int vio_hold_queue(vio_tap_state_t *tap, int x, int y, int ms)
{
    unsigned expected = 0u;
    unsigned packed;

    if(tap == NULL || x < 0 || y < 0 ||
       x > (int)VQEAF_TAP_COORD_MAX || y > (int)VQEAF_TAP_COORD_MAX)
        return -1;
    if(ms < VQEAF_HOLD_MS_MIN || ms > VQEAF_HOLD_MS_MAX)
        return -1;
    if(tap->hold_request != 0u || tap->swipe_request != 0ull) return -1;
    tap->hold_ms = (unsigned)ms;
    packed = vio_tap_pack(x, y) + 1u;
#if defined(__GNUC__) || defined(__clang__)
    if(!__atomic_compare_exchange_n(&tap->hold_request, &expected, packed, 0,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        return -1;
#else
    (void)expected;
    if(tap->hold_request != 0u) return -1;
    tap->hold_request = packed;
#endif
    return 0;
}

/* Presents one step of the synthetic contact:
 *   1  press at (*x, *y)
 *  -1  release
 *   0  nothing pending
 * A release is always returned before a newer queued tap, so the contact can
 * never go press -> press and swallow the click. */
static inline int vio_tap_present(vio_tap_state_t *tap, int *x, int *y)
{
    unsigned packed;

    if(tap == NULL) return 0;
    if(tap->release_pending) {
        /* A hold keeps the same point down until its deadline, which is what
         * makes LVGL's long-press timer fire on the injected contact. */
        if(tap->hold_until != 0ull) {
            if(vio_tap_now_ms() < tap->hold_until) {
                if(x) *x = tap->hold_x;
                if(y) *y = tap->hold_y;
                return 1;
            }
            tap->hold_until = 0ull;
            tap->release_pending = 0;
            return -1;
        }
        /* Keep the synthetic contact down for two more polls. A one-poll
         * press can be swallowed by a scrollable page's press handling, so
         * rows inside the content area never see the CLICK. */
        if(tap->hold_ticks > 0) {
            tap->hold_ticks--;
            if(x) *x = tap->hold_x;
            if(y) *y = tap->hold_y;
            return 1;
        }
        tap->release_pending = 0;
        return -1;
    }
#if defined(__GNUC__) || defined(__clang__)
    packed = __atomic_exchange_n(&tap->request, 0u, __ATOMIC_SEQ_CST);
#else
    packed = tap->request;
    tap->request = 0u;
#endif
    if(packed == 0u) {
#if defined(__GNUC__) || defined(__clang__)
        packed = __atomic_exchange_n(&tap->hold_request, 0u, __ATOMIC_SEQ_CST);
#else
        packed = tap->hold_request;
        tap->hold_request = 0u;
#endif
        if(packed == 0u) return 0;
        packed -= 1u;
        tap->hold_x = vio_tap_x(packed);
        tap->hold_y = vio_tap_y(packed);
        tap->hold_until = vio_tap_now_ms() + (unsigned long long)tap->hold_ms;
        tap->release_pending = 1;
        tap->hold_ticks = 0;
        if(x) *x = tap->hold_x;
        if(y) *y = tap->hold_y;
        return 1;
    }
    packed -= 1u;
    tap->hold_x = vio_tap_x(packed);
    tap->hold_y = vio_tap_y(packed);
    tap->hold_until = 0ull;
    if(x) *x = tap->hold_x;
    if(y) *y = tap->hold_y;
    tap->release_pending = 1;
    tap->hold_ticks = 2;
    return 1;
}

#endif /* VQEAF_VIOLET_TAP_H */
