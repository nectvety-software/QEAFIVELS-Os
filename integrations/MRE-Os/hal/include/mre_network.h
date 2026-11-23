#ifndef MRE_NETWORK_H
#define MRE_NETWORK_H
#include "mre_types.h"
typedef struct mre_net_address { uint8_t bytes[16]; uint8_t length; } mre_net_address_t;
typedef struct mre_network_ops {
    mre_status_t (*start)(void);
    mre_status_t (*stop)(void);
    mre_status_t (*transmit)(const void *frame, size_t length);
} mre_network_ops_t;
#endif
