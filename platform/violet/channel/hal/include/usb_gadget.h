/* SPDX-License-Identifier: MIT */
/* hal/include/usb_gadget.h - USB gadget (RNDIS/NCM mass storage LUN) */

#ifndef UMOS_USB_GADGET_H
#define UMOS_USB_GADGET_H

#include <stdint.h>
#include <stddef.h>

#define USB_GADGET_MAX_LUNS 1

/* The tmpfs image the configfs LUN points at. One path, two users: the volume
 * the phone edits in usb_gadget_poll() and the file f_mass_storage serves have
 * to be the same bytes, or a host command sits in the file while the phone
 * keeps answering from a copy it froze at boot. */
#define USB_GADGET_LUN_IMAGE "/run/qeaf-lun.img"

struct usb_gadget_lun {
    int (*read_sector)(uint32_t lba, uint8_t *buf);
    int (*write_sector)(uint32_t lba, const uint8_t *buf);
    uint32_t (*total_sectors)(void);
    const char *vendor;
    const char *product;
    const char *revision;
};

/* What the user picked in the "Use USB for" notification. On this platform the
 * rescue LUN is not a separate control channel: it IS the gadget's data
 * function (one f_mass_storage LUN over the RAM image). The shell therefore
 * offers two choices: CHARGE releases the UDC and the host sees nothing at all;
 * MTP keeps the drive visible. PTP is not offered at all - no responder exists
 * in this image, so vio_lun_set_mode() turns a request into MTP and logs it
 * instead of pretending; only the rescue verb `usb ptp` can still ask.
 *
 * Charge-only therefore hides QEAFRESCUE while it lasts, and the way back is
 * the Settings USB entry (no replug): the RAM volume is never destroyed, so the
 * same drive reappears as soon as a data mode is picked again. A reboot also
 * re-binds the drive, because the boot bind is file transfer. */
enum usb_mode {
    USB_MODE_CHARGE = 0,        /* VBUS only, no data endpoint */
    USB_MODE_MTP,               /* files  + control LUN */
    USB_MODE_PTP,               /* photos + control LUN */
    USB_MODE_COUNT
};

/* Returns the mode the gadget is actually in: MTP once a data function is
 * bound at boot, and CHARGE only after the user released the UDC. */
enum usb_mode usb_gadget_mode(void);
int  usb_gadget_set_mode(enum usb_mode mode);
const char *usb_gadget_mode_name(enum usb_mode mode);

/* VBUS present? On the board this reads the charger/USB PHY; the host gate
 * drives it through usb_gadget_set_cable() so the notification can be tested
 * against a real framebuffer without a phone. */
int  usb_gadget_cable(void);
void usb_gadget_set_cable(int plugged);

/* The RAM-backed volume the LUN serves. The rescue command loop reads and
 * writes QEAF.CMD / RESPONSE.TXT through this same object, so host writes
 * and phone answers are literally the same bytes. */
struct fat16_volume;
struct fat16_volume *usb_gadget_volume(void);
/* One tick of the QEAF.CMD reader; called from the init main loop. */
void usb_gadget_poll(void);

int usb_gadget_init(void);
int usb_gadget_start(void);
int usb_gadget_stop(void);
int usb_gadget_add_lun(struct usb_gadget_lun *lun);
int usb_gadget_set_rndis_mac(const uint8_t mac[6]);
int usb_gadget_set_ncm_mac(const uint8_t mac[6]);

/* The real bind (bsp/violet/usb_gadget_bind.c): configfs gadget, one
 * f_mass_storage LUN over USB_GADGET_LUN_IMAGE, then the UDC name found in
 * /sys/class/udc. usb_gadget_start() above only registers the LUN with this
 * process - without this bind the phone enumerates nothing on USB. */
int qeaf_gadget_bind(void);
/* Same, forked: a dwc3 probe or a slow tmpfs cannot park PID 1. */
int qeaf_gadget_start_detached(void);

#endif
