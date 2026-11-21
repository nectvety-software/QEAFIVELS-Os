/* SPDX-License-Identifier: MIT */
/* platform/violet/usb_lun.h - QEAFRESCUE FAT16 LUN over USB configfs. */
#ifndef HOMO_USB_LUN_H
#define HOMO_USB_LUN_H

#include <stdint.h>

#include "fs/fat16.h"
#include "hal/include/usb_gadget.h"

struct vio_lun {
    int fd;
    uint8_t *map;
    size_t size;
    struct fat16_volume volume;
    int ready;
    int gadget;
    /* The user chose "Chỉ sạc": keep the UDC released. Without this, main's
     * late-UDC retry rebinds the data function a few seconds later and the
     * phone silently reappears on the PC. */
    int charge_only;
    int cable;
    enum usb_mode mode;
    char udc[64];
};

/* CRASH.TXT is reserved at init so the UI fault handler can write its record
 * from a signal context without allocating clusters there. */
#define VIO_CRASH_CAP 8192
void vio_lun_reserve_crash(struct vio_lun *lun);
/* LASTBOOT.TXT: the record the previous boot left in the p77 journal slot, so
 * a phone that panicked still shows what happened after it comes back up. */
void vio_lun_reserve_journal(struct vio_lun *lun);

int vio_lun_init(struct vio_lun *lun);
int vio_lun_start_gadget(struct vio_lun *lun);
/* What the "Dùng USB cho" sheet does: charge releases the UDC (the phone only
 * draws current) and file re-binds it. A PTP request — only reachable from the
 * rescue verb `usb ptp`, since the shell does not offer it — is answered with
 * the drive and an honest log line, because this kernel has no responder.
 * Every choice is reversible without a reboot: the RAM volume is never
 * destroyed. */
int vio_lun_set_mode(struct vio_lun *lun, enum usb_mode mode);
void vio_lun_poll(struct vio_lun *lun);

#endif /* HOMO_USB_LUN_H */
