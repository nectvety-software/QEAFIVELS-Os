#include "include/vqeaf/input.h"
static VqeafInputEvent queue[VQEAF_INPUT_QUEUE_MAX];
static uint32_t head,tail,count;
void vqeaf_input_reset(void){head=tail=count=0;}
int vqeaf_input_push(const VqeafInputEvent *e){
 if(!e||!e->code||e->code>VQEAF_KEY_LOCK)return -1;
 if(count==VQEAF_INPUT_QUEUE_MAX)return -2;
 queue[tail]=*e;tail=(tail+1)%VQEAF_INPUT_QUEUE_MAX;count++;return 0;
}
int vqeaf_input_pop(VqeafInputEvent *out){
 if(!out)return -1;
 if(!count)return -2;
 *out=queue[head];head=(head+1)%VQEAF_INPUT_QUEUE_MAX;count--;return 0;
}
int vqeaf_input_pending(void){return (int)count;}
