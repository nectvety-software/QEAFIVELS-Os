/* SPDX-License-Identifier: MIT */
/* Host smoke test for the rescue injection queues (platform/violet/violet_tap.h).
 * The queue is OS-free on purpose so the press/release contract LVGL depends
 * on can be verified without a phone: a synthetic contact is held for three
 * polls before its release so a scrollable page does not swallow the CLICK,
 * a release always precedes the next press, a drag always walks from its
 * start to its end, invalid points are refused instead of truncated, and each
 * queue holds one request. */
#include <stdio.h>
#include <string.h>

#include "violet_tap.h"

static int failures;

#define CHECK(cond) do { \
    if(!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while(0)

/* The injected contact stays down for two extra polls (hold_ticks = 2). */
static void release_tap(vio_tap_state_t *tap)
{
    int x = -1;
    int y = -1;
    CHECK(vio_tap_present(tap, &x, &y) == 1);
    CHECK(vio_tap_present(tap, &x, &y) == 1);
    CHECK(vio_tap_present(tap, &x, &y) == -1);
}

int main(void)
{
    vio_tap_state_t tap;
    int x = -1;
    int y = -1;
    int i;
    int sx0 = -1, sy0 = -1, sx1 = -1, sy1 = -1, steps = -1;

    memset(&tap, 0, sizeof(tap));
    CHECK(vio_tap_present(&tap, &x, &y) == 0);

    CHECK(vio_tap_queue(&tap, 106, 1504) == 0);
    /* One slot: a tap that arrives before the UI consumes the first is refused. */
    CHECK(vio_tap_queue(&tap, 275, 1504) == -1);
    CHECK(vio_tap_present(&tap, &x, &y) == 1);
    CHECK(x == 106 && y == 1504);
    release_tap(&tap);  /* release */
    CHECK(vio_tap_present(&tap, &x, &y) == 0);

    /* A tap arriving while the previous contact is still down must not turn
     * into press -> press: the release comes first. */
    CHECK(vio_tap_queue(&tap, 10, 20) == 0);
    CHECK(vio_tap_present(&tap, &x, &y) == 1);
    CHECK(x == 10 && y == 20);
    CHECK(vio_tap_queue(&tap, 30, 40) == 0);
    release_tap(&tap);
    CHECK(vio_tap_present(&tap, &x, &y) == 1);
    CHECK(x == 30 && y == 40);
    release_tap(&tap);

    /* The 12-bit coordinate limit round-trips and rejects, never truncates. */
    CHECK(vio_tap_queue(&tap, 4095, 0) == 0);
    CHECK(vio_tap_present(&tap, &x, &y) == 1);
    CHECK(x == 4095 && y == 0);
    release_tap(&tap);
    CHECK(vio_tap_queue(&tap, 4096, 10) == -1);
    CHECK(vio_tap_queue(&tap, 10, 4096) == -1);
    CHECK(vio_tap_queue(&tap, -1, 10) == -1);
    CHECK(vio_tap_queue(&tap, 10, -1) == -1);
    CHECK(vio_tap_queue(NULL, 1, 1) == -1);

    /* (0,0) is a real canvas point, not "empty". */
    CHECK(vio_tap_queue(&tap, 0, 0) == 0);
    CHECK(vio_tap_present(&tap, &x, &y) == 1);
    CHECK(x == 0 && y == 0);
    release_tap(&tap);

    /* Steady state: each tap is three press polls then one release. */
    for(i = 0; i < 64; i++) {
        CHECK(vio_tap_queue(&tap, i, i * 2) == 0);
        CHECK(vio_tap_present(&tap, &x, &y) == 1);
        CHECK(x == i && y == i * 2);
        release_tap(&tap);
        CHECK(vio_tap_present(&tap, &x, &y) == 0);
    }

    /* Drag queue: one slot, 64-bit packing, independent of the tap slot. */
    CHECK(vio_swipe_queue(&tap, 360, 828, 360, 100, 8) == 0);
    CHECK(vio_swipe_queue(&tap, 1, 2, 3, 4, 8) == -1);
    CHECK(vio_swipe_take(&tap, &sx0, &sy0, &sx1, &sy1, &steps) == 1);
    CHECK(sx0 == 360 && sy0 == 828 && sx1 == 360 && sy1 == 100 && steps == 8);
    CHECK(vio_swipe_take(&tap, &sx0, &sy0, &sx1, &sy1, &steps) == 0);

    /* (0,0)->(0,0) survives the +1 bias and still round-trips. */
    CHECK(vio_swipe_queue(&tap, 0, 0, 0, 0, 2) == 0);
    CHECK(vio_swipe_take(&tap, &sx0, &sy0, &sx1, &sy1, &steps) == 1);
    CHECK(sx0 == 0 && sy0 == 0 && sx1 == 0 && sy1 == 0 && steps == 2);

    /* The 12-bit limit and the step range are enforced, never truncated. */
    CHECK(vio_swipe_queue(&tap, 0, 0, 4095, 4095, 64) == 0);
    CHECK(vio_swipe_take(&tap, &sx0, &sy0, &sx1, &sy1, &steps) == 1);
    CHECK(sx1 == 4095 && sy1 == 4095 && steps == 64);
    CHECK(vio_swipe_queue(&tap, 0, 0, 4096, 10, 8) == -1);
    CHECK(vio_swipe_queue(&tap, -1, 0, 10, 10, 8) == -1);
    CHECK(vio_swipe_queue(&tap, 0, 0, 10, 10, 1) == -1);
    CHECK(vio_swipe_queue(&tap, 0, 0, 10, 10, 65) == -1);
    CHECK(vio_swipe_queue(NULL, 0, 0, 10, 10, 8) == -1);

    /* Tap and drag have separate slots: queuing one does not eat the other. */
    CHECK(vio_tap_queue(&tap, 10, 20) == 0);
    CHECK(vio_swipe_queue(&tap, 0, 0, 10, 10, 4) == 0);
    CHECK(vio_tap_present(&tap, &x, &y) == 1);
    release_tap(&tap);
    CHECK(vio_swipe_take(&tap, &sx0, &sy0, &sx1, &sy1, &steps) == 1);
    CHECK(sx0 == 0 && sy0 == 0 && sx1 == 10 && sy1 == 10 && steps == 4);

    if(failures) {
        fprintf(stderr, "violet tap smoke: %d failure(s)\n", failures);
        return 1;
    }
    printf("violet tap smoke: PASS\n");
    return 0;
}
