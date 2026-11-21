#include <stdint.h>
#include <stdio.h>
#include "include/vqeaf/gfx.h"
int main(void){
 uint32_t pixels[64u*32u]={0};
 VqeafFramebuffer fb;
 if(vqeaf_gfx_setup(&fb,(uint64_t)(uintptr_t)pixels,64,32,64,VQEAF_GFX_FMT_BGR)!=0)return 1;
 const char *sample="Tiếng Việt";
 if(vqeaf_gfx_text_width(sample,1)!=120u)return 2;
 vqeaf_gfx_text(&fb,0,0,sample,1,0xFFFFFFFFu);
 uint32_t lit=0;for(uint32_t i=0;i<64u*32u;i++)if(pixels[i])lit++;
 if(lit<20u)return 3;
 const char malformed[]={(char)0xC0,(char)0xAF,0};
 if(vqeaf_gfx_text_width(malformed,1)!=24u)return 4;
 puts("Ubuntu UTF-8 Vietnamese framebuffer font PASS");
 return 0;
}
