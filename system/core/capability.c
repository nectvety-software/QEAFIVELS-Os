#include <string.h>
#include "include/vqeaf/capability.h"
static VqeafCapability caps[VQEAF_CAP_MAX];static uint32_t count;
void vqeaf_cap_reset(void){memset(caps,0,sizeof(caps));count=0;}
int vqeaf_cap_set(const char*n,VqeafCapabilityState s,uint32_t p,int32_t d){if(!n||!*n||strlen(n)>=sizeof(caps[0].name)||s>VQEAF_CAP_ERROR)return -1;uint32_t i;for(i=0;i<count;i++)if(strcmp(caps[i].name,n)==0)break;if(i==count){if(count==VQEAF_CAP_MAX)return -2;count++;}strcpy(caps[i].name,n);caps[i].state=s;caps[i].provider=p;caps[i].detail=d;return 0;}
int vqeaf_cap_get(const char*n,VqeafCapability*out){if(!n||!out)return -1;for(uint32_t i=0;i<count;i++)if(strcmp(caps[i].name,n)==0){*out=caps[i];return 0;}return -2;}
int vqeaf_cap_is_ready(const char*n){VqeafCapability c;return vqeaf_cap_get(n,&c)==0&&c.state==VQEAF_CAP_READY;}
