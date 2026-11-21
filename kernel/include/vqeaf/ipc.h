#ifndef VQEAF_IPC_H
#define VQEAF_IPC_H
#include <stdint.h>
#define VQEAF_IPC_QUEUE_CAPACITY 16u
#define VQEAF_IPC_PAYLOAD_MAX 64u
typedef struct { uint32_t sender,type,size; uint8_t payload[VQEAF_IPC_PAYLOAD_MAX]; } VqeafMessage;
typedef struct { VqeafMessage messages[VQEAF_IPC_QUEUE_CAPACITY]; uint32_t head,tail,count; } VqeafIpcQueue;
void vqeaf_ipc_init(VqeafIpcQueue *q);int vqeaf_ipc_send(VqeafIpcQueue *q,const VqeafMessage *m);int vqeaf_ipc_receive(VqeafIpcQueue *q,VqeafMessage *out);
#endif
