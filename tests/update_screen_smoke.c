#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../system/ui/include/vqeaf/gfx.h"
#include "../system/ui/include/vqeaf/update_screen.h"
static uint32_t portrait_px[720u * 1280u];
int main(void){
 static uint32_t pixels[240u*320u];
 VqeafFramebuffer fb;
 assert(vqeaf_gfx_setup(&fb,(uint64_t)(uintptr_t)pixels,240,320,240,VQEAF_GFX_FMT_BGR)==0);
 vqeaf_update_screen_render(&fb,37,"Xác minh gói cập nhật",0);
 uint32_t changed=0;for(uint32_t i=0;i<240u*320u;i++)if(pixels[i])changed++;
 assert(changed>1000);
 vqeaf_update_screen_render(&fb,100,"Hoàn tất",0);
 vqeaf_update_screen_render(&fb,0,"Không thể xác minh chữ ký",1);

 /* 720x1280 portrait is the shipped framebuffer. Every stage label must fit
  * on one line, so the renderer drops 2x text to 1x; nothing may be painted
  * in the right third of the first stage rows (that is where a clipped 2x
  * label used to run off the panel). */
 assert(vqeaf_gfx_setup(&fb,(uint64_t)(uintptr_t)portrait_px,720,1280,720,
                        VQEAF_GFX_FMT_BGR)==0);
 uint32_t margin = 720u / 12u;
 uint32_t label_x = margin + 18u * 2u;
 for (uint32_t k = 0; k < 4u; k++) {
  const char *label = vqeaf_update_stage_label(k);
  assert(label && *label);
  assert(label_x + vqeaf_gfx_text_width(label, 1u) <= 720u);
 }
 memset(portrait_px, 0, sizeof portrait_px);
 vqeaf_update_screen_render(&fb, 0, "Không thể cập nhật", 0);
 uint32_t bg = vqeaf_gfx_rgb(&fb, 0x050B12);
 for (uint32_t y = 440u; y < 490u; y++)
  for (uint32_t x = 450u; x < 720u; x++)
   assert(portrait_px[y * 720u + x] == bg);
 return 0;
}
