/* SPDX-License-Identifier: MIT */
/* platform/violet/bt_bringup.h - read-only Linux Bluetooth evidence for violet. */
#ifndef VIO_BT_BRINGUP_H
#define VIO_BT_BRINGUP_H

#ifdef __cplusplus
extern "C" {
#endif

#define VIO_BT_DT_NODE   "/proc/device-tree/soc/bt_wcn3990"
#define VIO_BT_SLIM_NODE "/proc/device-tree/soc/slim@62e40000/wcn3990"
#define VIO_BT_SLIM_CTRL "/proc/device-tree/soc/slim@62e40000"

void vio_bt_report(char *out, int cap);
void vio_bt_diag(char *out, int cap);
void vio_bt_firmware_report(char *out, int cap);
void vio_bt_slim_report(char *out, int cap);

#ifdef __cplusplus
}
#endif

#endif /* VIO_BT_BRINGUP_H */
