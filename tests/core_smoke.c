#include "../system/init/init.h"
#include "../framework/application_api/qeaf_api.h"
#include <assert.h>
int main(void){
 vqeaf_init_context c;vqeaf_init_reset(&c);
 assert(c.state==VQEAF_INIT_COLD);
 assert(vqeaf_init_transition(&c,VQEAF_INIT_EARLY)==0);
 assert(vqeaf_init_transition(&c,VQEAF_INIT_SERVICES)==0);
 assert(vqeaf_init_transition(&c,VQEAF_INIT_READY)==0);
 assert(vqeaf_init_transition(&c,VQEAF_INIT_EARLY)!=0);
 assert(c.transitions==3);
 vqeaf_init_fail(&c,17);assert(c.state==VQEAF_INIT_FAILED&&c.last_error==17);
 assert(vqeaf_framework_abi_version()==VQEAF_FRAMEWORK_ABI_VERSION);
 return 0;
}
