#include "../include/vqeaf/ipc.h"
void vqeaf_ipc_init(VqeafIpcQueue*q){if(q)q->head=q->tail=q->count=0;}
int vqeaf_ipc_send(VqeafIpcQueue*q,const VqeafMessage*m){if(!q||!m||m->size>VQEAF_IPC_PAYLOAD_MAX)return -1;if(q->count==VQEAF_IPC_QUEUE_CAPACITY)return -2;q->messages[q->tail]=*m;q->tail=(q->tail+1)%VQEAF_IPC_QUEUE_CAPACITY;q->count++;return 0;}
int vqeaf_ipc_receive(VqeafIpcQueue*q,VqeafMessage*out){if(!q||!out)return -1;if(!q->count)return -2;*out=q->messages[q->head];q->head=(q->head+1)%VQEAF_IPC_QUEUE_CAPACITY;q->count--;return 0;}
