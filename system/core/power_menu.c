#include "include/vqeaf/power_menu.h"
static const char *item_names[VQEAF_PMENU_ITEMS]={"Power off","Restart","Fastboot","Sleep"};
static const VqeafPowerAction item_actions[VQEAF_PMENU_ITEMS]={VQEAF_PMENU_POWER_OFF,VQEAF_PMENU_RESTART,VQEAF_PMENU_FASTBOOT,VQEAF_PMENU_SLEEP};
void vqeaf_pmenu_init(VqeafPowerMenu *m){if(m){m->state=VQEAF_PMENU_CLOSED;m->selected=0;}}
void vqeaf_pmenu_open(VqeafPowerMenu *m){if(m){m->state=VQEAF_PMENU_OPEN;m->selected=0;}}
const char *vqeaf_pmenu_item_name(uint32_t index){return index<VQEAF_PMENU_ITEMS?item_names[index]:0;}
VqeafPowerAction vqeaf_pmenu_item_action(uint32_t index){return index<VQEAF_PMENU_ITEMS?item_actions[index]:VQEAF_PMENU_NONE;}
int vqeaf_pmenu_handle(VqeafPowerMenu *m,const VqeafInputEvent *e){
 if(!m||!e)return -1;
 if(m->state==VQEAF_PMENU_CLOSED){
  if(e->code==VQEAF_KEY_POWER&&e->gesture==VQEAF_KEY_LONG_PRESS){m->state=VQEAF_PMENU_OPEN;m->selected=0;}
  return VQEAF_PMENU_NONE;
 }
 switch(e->code){
 case VQEAF_KEY_VOLUME_UP:  m->selected=(m->selected+VQEAF_PMENU_ITEMS-1)%VQEAF_PMENU_ITEMS;break;
 case VQEAF_KEY_VOLUME_DOWN:m->selected=(m->selected+1)%VQEAF_PMENU_ITEMS;break;
 case VQEAF_KEY_POWER:
  if(e->gesture==VQEAF_KEY_LONG_PRESS){m->state=VQEAF_PMENU_CLOSED;return VQEAF_PMENU_NONE;} /* cancel */
  { /* short press confirms the highlighted item */
   VqeafPowerAction a=item_actions[m->selected];
   m->state=VQEAF_PMENU_CLOSED;m->selected=0;return (int)a;
  }
 default:break;
 }
 return VQEAF_PMENU_NONE;
}
