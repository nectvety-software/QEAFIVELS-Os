#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "mre_ui.h"
static uint32_t text_calls;
static void test_draw_text(void *ctx,uint16_t *fb,uint32_t w,uint32_t h,uint32_t stride,
 int32_t x,int32_t y,uint32_t scale,uint16_t color,const char *s) {
 (void)ctx;(void)fb;(void)w;(void)h;(void)stride;(void)x;(void)y;(void)scale;(void)color;
 assert(s!=NULL); ++text_calls;
}
int main(void) {
 const uint32_t w=360,h=780,stride=360;
 uint16_t *fb=(uint16_t *)calloc((size_t)stride*h,sizeof(uint16_t));
 mre_ui_state_t state={"09:41","23 tháng 11","Chưa xác minh",75u,0u,0u,0u};
 mre_ui_font_ops_t font={test_draw_text,NULL};
 assert(fb!=NULL);
 assert(mre_ui_render_home(fb,w,h,stride,&state,&font)==MRE_OK);
 assert(text_calls>=10u); assert(fb[0]!=0u);
 assert(mre_ui_hit_test(w,h,80,360)==MRE_UI_ACTION_PHONE);
 assert(mre_ui_hit_test(w,h,260,360)==MRE_UI_ACTION_MESSAGES);
 assert(mre_ui_hit_test(w,h,80,500)==MRE_UI_ACTION_FILES);
 assert(mre_ui_hit_test(w,h,260,500)==MRE_UI_ACTION_SETTINGS);
 assert(mre_ui_hit_test(w,h,10,10)==MRE_UI_ACTION_QUICK_SETTINGS);
 assert(mre_ui_hit_test(w,h,-1,10)==MRE_UI_ACTION_NONE);
 assert(mre_ui_render_home(NULL,w,h,stride,&state,&font)==MRE_E_INVALID);
 free(fb); puts("MRE UI tests passed"); return 0;
}
