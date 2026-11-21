#ifndef VQEAF_SYSTEM_INIT_H
#define VQEAF_SYSTEM_INIT_H
typedef enum { VQEAF_INIT_COLD=0, VQEAF_INIT_EARLY=1, VQEAF_INIT_SERVICES=2, VQEAF_INIT_READY=3, VQEAF_INIT_FAILED=255 } vqeaf_init_state;
typedef struct { vqeaf_init_state state; int last_error; unsigned transitions; } vqeaf_init_context;
void vqeaf_init_reset(vqeaf_init_context *ctx);
int vqeaf_init_transition(vqeaf_init_context *ctx,vqeaf_init_state next);
void vqeaf_init_fail(vqeaf_init_context *ctx,int error);
const char *vqeaf_init_state_name(vqeaf_init_state state);
#endif
