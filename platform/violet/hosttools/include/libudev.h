/* Minimal stand-in for libudev for QEAF's static rmtfs build.
 * Only the handful of calls used by upstream rmtfs/sharedmem.c are present;
 * it reads the same sysfs attributes libudev would read. */
#ifndef QEAF_LIBUDEV_STUB_H
#define QEAF_LIBUDEV_STUB_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct udev;
struct udev_device;

struct udev *udev_new(void);
void udev_unref(struct udev *udev);
struct udev_device *udev_device_new_from_devnum(struct udev *udev, char type,
                                                dev_t devnum);
const char *udev_device_get_sysattr_value(struct udev_device *dev,
                                          const char *attr);
void udev_device_unref(struct udev_device *dev);

#ifdef __cplusplus
}
#endif

#endif
