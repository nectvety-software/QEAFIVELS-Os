#ifndef VQEAF_INPUT_H
#define VQEAF_INPUT_H
#include <stdint.h>
/* Input event service v1: hardware buttons (volume, power) as bounded queue.
 * Sources translate raw signals into gestures; consumers read events. */
#define VQEAF_INPUT_QUEUE_MAX 16u
typedef enum {
 VQEAF_KEY_VOLUME_UP=1,VQEAF_KEY_VOLUME_DOWN=2,VQEAF_KEY_POWER=3,
 VQEAF_KEY_NAV_UP=4,VQEAF_KEY_NAV_DOWN=5,VQEAF_KEY_NAV_LEFT=6,VQEAF_KEY_NAV_RIGHT=7,
 VQEAF_KEY_ENTER=8,VQEAF_KEY_BACK=9,VQEAF_KEY_QUICK=10,VQEAF_KEY_LOCK=11
} VqeafKeyCode;
typedef enum { VQEAF_KEY_TAP=0,VQEAF_KEY_LONG_PRESS=1 } VqeafKeyGesture;
typedef struct { VqeafKeyCode code; VqeafKeyGesture gesture; } VqeafInputEvent;
void vqeaf_input_reset(void);
int vqeaf_input_push(const VqeafInputEvent *e);   /* 0 ok; -1 arg; -2 full */
int vqeaf_input_pop(VqeafInputEvent *out);        /* 0 ok; -1 arg; -2 empty */
int vqeaf_input_pending(void);
#endif
