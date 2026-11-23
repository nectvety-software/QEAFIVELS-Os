#include "mre_ui.h"
#define C_BG 0x1082u
#define C_PANEL 0x2104u
#define C_PANEL_2 0x2945u
#define C_ACCENT 0x5D7Fu
#define C_TEXT 0xFFFFu
#define C_MUTED 0xBDF7u
#define C_GREEN 0x6F4Du
#define C_DARK 0x0861u
static void fill_rect(uint16_t *fb,uint32_t width,uint32_t height,uint32_t stride,
 int32_t x,int32_t y,int32_t w,int32_t h,uint16_t color) {
 int32_t x0=x<0?0:x, y0=y<0?0:y, x1=x+w, y1=y+h, row;
 if(x1>(int32_t)width)x1=(int32_t)width;
 if(y1>(int32_t)height)y1=(int32_t)height;
 if(x0>=x1||y0>=y1)return;
 for(row=y0;row<y1;++row) {
  uint16_t *dst=fb+(size_t)row*stride+(size_t)x0; int32_t col;
  for(col=x0;col<x1;++col)*dst++=color;
 }
}
static int32_t sx(uint32_t w,int32_t v){return (int32_t)(((int64_t)w*v)/1080);}
static int32_t sy(uint32_t h,int32_t v){return (int32_t)(((int64_t)h*v)/2340);}
static void draw_text(const mre_ui_font_ops_t *font,uint16_t *fb,uint32_t w,
 uint32_t h,uint32_t stride,int32_t x,int32_t y,uint32_t scale,
 uint16_t color,const char *s) {
 if(font&&font->draw_text&&s)font->draw_text(font->context,fb,w,h,stride,x,y,scale,color,s);
}
static mre_ui_rect_t app_rect(uint32_t w,uint32_t h,uint32_t i) {
 const int32_t margin=56,gap=24,top=1000,card_h=270;
 int32_t card_w=(1080-margin*2-gap)/2,col=(int32_t)(i&1u),row=(int32_t)(i>>1);
 mre_ui_rect_t r;
 r.x=sx(w,margin+col*(card_w+gap)); r.y=sy(h,top+row*(card_h+gap));
 r.width=sx(w,card_w); r.height=sy(h,card_h); return r;
}
mre_status_t mre_ui_render_home(uint16_t *fb,uint32_t w,uint32_t h,uint32_t stride,
 const mre_ui_state_t *state,const mre_ui_font_ops_t *font) {
 uint32_t i; int32_t scale;
 static const char *const labels[4]={"Điện thoại","Tin nhắn","Tệp của tôi","Cài đặt"};
 static const char *const hints[4]={"Cuộc gọi và danh bạ","Trao đổi riêng tư",
  "Bộ nhớ và tài liệu","Hệ thống và thiết bị"};
 if(!fb||w<240u||h<320u||stride<w||!state)return MRE_E_INVALID;
 scale=(int32_t)(w/360u); if(scale<1)scale=1;
 fill_rect(fb,w,h,stride,0,0,(int32_t)w,(int32_t)h,C_BG);
 fill_rect(fb,w,h,stride,0,0,(int32_t)w,sy(h,88),C_DARK);
 draw_text(font,fb,w,h,stride,sx(w,56),sy(h,24),(uint32_t)scale,C_TEXT,
  state->clock_text?state->clock_text:"--:--");
 draw_text(font,fb,w,h,stride,sx(w,790),sy(h,24),(uint32_t)scale,C_MUTED,
  state->status_text?state->status_text:"Trạng thái hệ thống");
 draw_text(font,fb,w,h,stride,sx(w,64),sy(h,250),(uint32_t)(scale*2),C_TEXT,"MRE OS");
 draw_text(font,fb,w,h,stride,sx(w,64),sy(h,340),(uint32_t)scale,C_MUTED,
  state->date_text?state->date_text:"Chào mừng");
 fill_rect(fb,w,h,stride,sx(w,64),sy(h,470),sx(w,952),sy(h,112),C_PANEL);
 draw_text(font,fb,w,h,stride,sx(w,88),sy(h,494),(uint32_t)scale,C_TEXT,"Trạng thái thiết bị");
 draw_text(font,fb,w,h,stride,sx(w,88),sy(h,540),(uint32_t)scale,C_MUTED,
  state->wifi_connected?"Wi-Fi: Đã kết nối":"Wi-Fi: Chưa xác minh");
 draw_text(font,fb,w,h,stride,sx(w,520),sy(h,540),(uint32_t)scale,C_MUTED,
  state->cellular_available?"Di động: Sẵn sàng":"Di động: Chưa xác minh");
 draw_text(font,fb,w,h,stride,sx(w,820),sy(h,540),(uint32_t)scale,C_MUTED,
  state->charging?"Đang sạc":"Pin");
 draw_text(font,fb,w,h,stride,sx(w,64),sy(h,870),(uint32_t)scale,C_TEXT,"Ứng dụng");
 for(i=0;i<MRE_UI_APP_COUNT;++i) {
  mre_ui_rect_t r=app_rect(w,h,i); int32_t pad=sx(w,28);
  fill_rect(fb,w,h,stride,r.x,r.y,r.width,r.height,i==3u?C_PANEL_2:C_PANEL);
  fill_rect(fb,w,h,stride,r.x+pad,r.y+pad,sx(w,76),sy(h,76),i==3u?C_ACCENT:C_GREEN);
  draw_text(font,fb,w,h,stride,r.x+pad,r.y+sy(h,132),(uint32_t)scale,C_TEXT,labels[i]);
  draw_text(font,fb,w,h,stride,r.x+pad,r.y+sy(h,188),(uint32_t)scale,C_MUTED,hints[i]);
 }
 fill_rect(fb,w,h,stride,sx(w,420),sy(h,2260),sx(w,240),sy(h,12),C_MUTED);
 draw_text(font,fb,w,h,stride,sx(w,64),sy(h,2140),(uint32_t)scale,C_MUTED,
  "Vuốt lên để mở danh sách ứng dụng");
 return MRE_OK;
}
mre_ui_action_t mre_ui_hit_test(uint32_t w,uint32_t h,int32_t x,int32_t y) {
 uint32_t i;
 if(w<240u||h<320u||x<0||y<0||x>=(int32_t)w||y>=(int32_t)h)return MRE_UI_ACTION_NONE;
 for(i=0;i<MRE_UI_APP_COUNT;++i) {
  mre_ui_rect_t r=app_rect(w,h,i);
  if(x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height)
   return (mre_ui_action_t)(MRE_UI_ACTION_PHONE+i);
 }
 if(y<sy(h,88))return MRE_UI_ACTION_QUICK_SETTINGS;
 return MRE_UI_ACTION_NONE;
}
