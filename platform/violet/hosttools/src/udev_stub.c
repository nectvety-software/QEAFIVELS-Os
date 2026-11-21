/* SPDX-License-Identifier: MIT */
/* libudev shim for the static rmtfs build. rmtfs only asks for the sysfs
 * attributes of a character device; this answers from /sys/dev directly. */
#include "libudev.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <unistd.h>

struct udev {
    int dummy;
};

struct udev_device {
    char path[256];
};

struct udev *udev_new(void)
{
    return calloc(1, sizeof(struct udev));
}

void udev_unref(struct udev *udev)
{
    free(udev);
}

struct udev_device *udev_device_new_from_devnum(struct udev *udev, char type,
                                                dev_t devnum)
{
    struct udev_device *dev;

    (void)udev;
    if(type != 'c') return NULL;
    dev = calloc(1, sizeof(*dev));
    if(!dev) return NULL;
    snprintf(dev->path, sizeof(dev->path), "/sys/dev/char/%u:%u",
             (unsigned)major(devnum), (unsigned)minor(devnum));
    return dev;
}

void udev_device_unref(struct udev_device *dev)
{
    free(dev);
}

const char *udev_device_get_sysattr_value(struct udev_device *dev,
                                          const char *attr)
{
    static char value[128];
    char path[384];
    int fd, n;

    if(!dev || !attr) return NULL;
    if(snprintf(path, sizeof(path), "%s/%s", dev->path, attr) >=
       (int)sizeof(path)) return NULL;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        /* Some kernels hang the attributes one level below the char dev. */
        if(snprintf(path, sizeof(path), "%s/device/%s", dev->path, attr) >=
           (int)sizeof(path)) return NULL;
        fd = open(path, O_RDONLY | O_CLOEXEC);
    }
    if(fd < 0) return NULL;
    n = read(fd, value, sizeof(value) - 1);
    close(fd);
    if(n <= 0) return NULL;
    value[n] = '\0';
    while(n > 0 && (value[n - 1] == '\n' || value[n - 1] == '\r'))
        value[--n] = '\0';
    return value;
}
