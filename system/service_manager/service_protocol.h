#ifndef VQEAF_SERVICE_PROTOCOL_H
#define VQEAF_SERVICE_PROTOCOL_H
#include <stdint.h>
#define VQEAF_SERVICE_IPC_VERSION 1u
typedef struct { uint32_t version,size,service_id,operation; uint64_t request_id; } vqeaf_service_message_v1;
#endif
