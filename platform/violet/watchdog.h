/* SPDX-License-Identifier: MIT */
/* platform/violet/watchdog.h - keep the SoC watchdog timer fed from a loop
 * that is provably alive.
 *
 * The stock violet kernel arrives from the bootloader with the Qualcomm
 * watchdog armed; vendor init opens /dev/watchdog and pets it. This
 * image is PID 1 and never did, so the timer expired ~40 s into every boot
 * (the p77 journal showed boots=454 with no crash record at all). Feeding it
 * is not cosmetic: without it the OS cannot stay up long enough to bring the
 * radio up or finish a device check.
 *
 * The pet is gated on progress, not on the thread doing the petting: the main
 * loop calls vio_watchdog_ack() every iteration and only a recent ack lets
 * vio_watchdog_pet() touch the device. A genuinely wedged OS still resets.
 */
#ifndef VIO_WATCHDOG_H
#define VIO_WATCHDOG_H

/* Opens /dev/watchdog (or watchdog0) and asks for a 30 s timeout. Returns 0
 * when the device answered, -1 when the kernel exposes no watchdog - logged
 * once, never retried in a loop. */
int vio_watchdog_start(void);

/* Called by the main loop once per iteration, including while the screen
 * sleeps: that loop is what has to keep running. */
void vio_watchdog_ack(void);

/* Called by the same loop; pets at most every 5 s and only while the last ack
 * is recent, so a stuck main loop stops feeding the timer. */
void vio_watchdog_pet(void);

#endif /* VIO_WATCHDOG_H */
