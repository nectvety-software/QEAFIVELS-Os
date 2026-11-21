/* QEAF Maps - native QEAPP ABI v3 sample.
 * Static, offline map illustration inspired by the supplied screenshot.
 * No GPS, network, map tiles, geocoder or route service is claimed.
 * All display access goes through the framework API; no syscalls/libc.
 */
#include "qeaf_api.h"
#define MAP_W 720
#define MAP_H 1560
#define C_BG 0xEF7B
#define C_ROAD 0xFFFF
#define C_ROAD_EDGE 0xD6BA
#define C_PARK 0xB7F2
#define C_PARK_DK 0x8E8A
#define C_TEXT 0x2945
#define C_RED 0xD986
#define C_WHITE 0xFFFF
#define C_PANEL 0xFFFF
#define C_LINE 0xDEFB

static const qeaf_app_api_v3 *g_api;
static uint16_t g_pixels[MAP_W * MAP_H];

static void rect(int x, int y, int w, int h, uint16_t c)
{
    if(w <= 0 || h <= 0) return;
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > MAP_W) w = MAP_W - x;
    if(y + h > MAP_H) h = MAP_H - y;
    if(w <= 0 || h <= 0) return;
    for(int yy=y; yy<y+h; ++yy)
        for(int xx=x; xx<x+w; ++xx) g_pixels[yy*MAP_W+xx]=c;
}
static void line(int x0,int y0,int x1,int y1,int thick,uint16_t c)
{
    int dx=x1-x0,dy=y1-y0,steps=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    if(ay>steps) steps=ay;
    if(steps<1) { rect(x0,y0,thick,thick,c); return; }
    for(int i=0;i<=steps;++i)
        rect(x0+dx*i/steps-thick/2,y0+dy*i/steps-thick/2,thick,thick,c);
}
static void road(int x0,int y0,int x1,int y1,int width)
{
    line(x0,y0,x1,y1,width+8,C_ROAD_EDGE);
    line(x0,y0,x1,y1,width,C_ROAD);
}
static void pin(int x,int y)
{
    rect(x-15,y-15,30,30,C_WHITE);
    rect(x-11,y-11,22,22,C_RED);
    rect(x-5,y-5,10,10,C_WHITE);
    line(x-8,y+8,x,y+22,7,C_RED);
    line(x+8,y+8,x,y+22,7,C_RED);
    rect(x-3,y-3,6,6,C_WHITE);
}
static void label(int x,int y,const char *s)
{
    if(g_api && g_api->draw_text) (void)g_api->draw_text(x,y,s);
}
static void render_map(void)
{
    for(int i=0;i<MAP_W*MAP_H;++i) g_pixels[i]=C_BG;
    rect(0,1110,250,450,C_PARK);
    line(20,1150,220,1510,9,C_PARK_DK);
    line(30,1430,220,1190,7,C_PARK_DK);
    rect(70,1240,24,24,C_PARK_DK);
    rect(165,1370,20,20,C_PARK_DK);

    road(80,-30,130,1050,34);
    road(330,-40,290,1090,42);
    road(570,-30,520,1100,38);
    road(700,80,440,520,28);
    road(-30,280,740,230,32);
    road(-20,620,740,590,38);
    road(-30,900,740,860,32);
    road(220,1090,700,1060,30);
    road(150,340,540,740,20);
    road(540,300,350,720,18);

    rect(165,75,86,100,0xE7B9);
    rect(390,70,112,92,0xE7B9);
    rect(35,390,95,125,0xE7B9);
    rect(390,360,112,130,0xE7B9);
    rect(585,690,95,105,0xE7B9);
    rect(365,940,100,78,0xE7B9);
    rect(35,960,130,100,0xE7B9);

    road(80,-30,130,1050,34);
    road(330,-40,290,1090,42);
    road(570,-30,520,1100,38);
    road(700,80,440,520,28);
    road(-30,280,740,230,32);
    road(-20,620,740,590,38);
    road(-30,900,740,860,32);
    road(220,1090,700,1060,30);

    pin(96,170); pin(590,320); pin(470,490);
    pin(170,680); pin(300,720); pin(390,690);
    pin(470,760); pin(535,790); pin(110,870); pin(640,940);

    label(160,112,"Oasis");
    label(160,145,"Basic convenience");
    label(365,360,"Taqueria La Nortena");
    label(365,393,"Mexican food");
    label(505,610,"Fortune Cookies");
    label(505,643,"Asian takeout");
    label(420,810,"Raizes Churrascar");
    label(420,843,"Brazilian food");
    label(24,1095,"McCarren Park");
    label(34,1130,"Green space");

    rect(0,1450,MAP_W,110,C_PANEL);
    rect(0,1450,MAP_W,2,C_LINE);
    label(58,1494,"SHOW LIST");
    label(58,1530,"Nearby places | Sample map");
    rect(620,1350,72,72,C_WHITE);
    rect(642,1372,28,28,C_LINE);
    rect(651,1381,10,10,0x2D7F);
}
void *create(const qeaf_app_api_v3 *api)
{
    if(api==0 || api->abi_version<VQEAF_APP_API_V3_VERSION ||
       api->size<sizeof(qeaf_app_api_v3) || api->blit_pixels==0 ||
       api->draw_text==0) return 0;
    g_api=api;
    return &g_api;
}
int start(void *handle)
{
    (void)handle;
    if(g_api==0) return -1;
    render_map();
    return g_api->blit_pixels(0,0,MAP_W,MAP_H,MAP_W,g_pixels);
}
int pause(void *handle) { (void)handle; return 0; }
int resume(void *handle) { return start(handle); }
int stop(void *handle) { (void)handle; return 0; }
void destroy(void *handle) { (void)handle; g_api=0; }
