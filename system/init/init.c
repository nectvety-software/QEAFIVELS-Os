#include "init.h"
void vqeaf_init_reset(vqeaf_init_context *ctx){if(!ctx)return;ctx->state=VQEAF_INIT_COLD;ctx->last_error=0;ctx->transitions=0;}
int vqeaf_init_transition(vqeaf_init_context *ctx,vqeaf_init_state next){
 if(!ctx||ctx->state==VQEAF_INIT_FAILED||next==VQEAF_INIT_FAILED)return -1;
 if(next!=(vqeaf_init_state)(ctx->state+1)||next>VQEAF_INIT_READY)return -2;
 ctx->state=next;ctx->transitions++;return 0;
}
void vqeaf_init_fail(vqeaf_init_context *ctx,int error){if(!ctx)return;ctx->state=VQEAF_INIT_FAILED;ctx->last_error=error;ctx->transitions++;}
const char *vqeaf_init_state_name(vqeaf_init_state s){
 switch(s){case VQEAF_INIT_COLD:return "cold";case VQEAF_INIT_EARLY:return "early";case VQEAF_INIT_SERVICES:return "services";case VQEAF_INIT_READY:return "ready";default:return "failed";}
}
