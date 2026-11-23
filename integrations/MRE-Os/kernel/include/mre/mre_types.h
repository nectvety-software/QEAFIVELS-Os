#ifndef MRE_TYPES_H
#define MRE_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef enum mre_status {
    MRE_OK = 0,
    MRE_E_INVALID = -1,
    MRE_E_NOMEM = -2,
    MRE_E_EXISTS = -3,
    MRE_E_NOT_FOUND = -4,
    MRE_E_UNSUPPORTED = -5,
    MRE_E_IO = -6,
    MRE_E_PERMISSION = -7,
    MRE_E_VERSION = -8,
    MRE_E_CHECKSUM = -9,
    MRE_E_BUSY = -10
} mre_status_t;

#define MRE_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#endif
