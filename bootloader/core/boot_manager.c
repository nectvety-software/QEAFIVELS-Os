#include "efi_minimal.h"
#include "display_mode.h"
#include "../../kernel/include/vqeaf/boot_protocol.h"
#include "../../kernel/include/vqeaf/serial.h"
#include "../../system/ui/include/vqeaf/gfx.h"
#include "../../system/ui/include/vqeaf/font8x8.h"
#include "../../system/core/include/vqeaf/version.h"
static EFI_GUID loaded_guid={{0xa1,0x31,0x1b,0x5b,0x62,0x95,0xd2,0x11,0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID fs_guid={{0x22,0x5b,0x4e,0x96,0x59,0x64,0xd2,0x11,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
/* EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID: 9042A9DE-23DC-4A38-96FB-7ADED080516A */
static EFI_GUID gop_guid={{0xde,0xa9,0x42,0x90,0xdc,0x23,0x38,0x4a,0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}};
static CHAR16 title[]=L"VQEAF Boot Manager 0.3\r\n",loading[]=L"Loading native kernel image...\r\n",failed[]=L"BOOT FAILED: KERNEL.EFI unavailable or invalid.\r\n",returned[]=L"Kernel returned to boot manager.\r\n",path[]=L"\\EFI\\VQEAF\\KERNEL.EFI";
static void serial_hex(uint64_t v){
 static const char h[]="0123456789ABCDEF";
 vqeaf_serial_write("0x");
 for(int s=60;s>=0;s-=4)vqeaf_serial_char(h[(v>>s)&15]);
}
static void splash_delay(EFI_SYSTEM_TABLE *st,UINTN microseconds){
 EFI_STALL stall=(EFI_STALL)BS(st)->Stall;
 if(stall)stall(microseconds);
}
/* Discover GOP, select the shipped portrait 9:16 display (see display_mode.h),
 * record the linear framebuffer in the v3 handoff and draw the boot splash.
 * If no usable framebuffer exists the boot continues text-only. */
static int splash_init(EFI_SYSTEM_TABLE *st,VqeafBootInfoV3 *info,VqeafFramebuffer *fb){
 EFI_GRAPHICS_OUTPUT_PROTOCOL *gop=0;
 EFI_STATUS s=((EFI_LOCATE_PROTOCOL)BS(st)->LocateProtocol)(&gop_guid,0,(void**)&gop);
 if(EFI_ERROR(s)||!gop)
  s=((EFI_HANDLE_PROTOCOL)BS(st)->HandleProtocol)(st->ConsoleOutHandle,&gop_guid,(void**)&gop);
 if(EFI_ERROR(s)||!gop||!gop->Mode||!gop->Mode->Info){
  vqeaf_serial_write("GOP: no graphics output protocol; serial-only boot\r\n");
  goto unavailable;
 }
 VqeafDisplayChoice display;
 if(vqeaf_display_choose_portrait(st,gop,&display)!=0||
    !display.base||!display.bytes||
    vqeaf_gfx_setup(fb,display.base,display.width,display.height,display.stride,display.format)!=0){
  vqeaf_serial_write("GOP: unsupported pixel format or framebuffer; serial-only boot\r\n");
  goto unavailable;
 }
 info->framebuffer_base=display.base;info->framebuffer_size=display.bytes;
 info->framebuffer_width=display.width;info->framebuffer_height=display.height;
 info->framebuffer_stride=display.stride;info->framebuffer_format=display.format;
 vqeaf_serial_write("GOP: framebuffer base=");serial_hex(display.base);
 vqeaf_serial_write(" bytes=");serial_hex(display.bytes);
 vqeaf_serial_write(" mode=");serial_hex(display.width);vqeaf_serial_char('x');
 serial_hex(display.height);vqeaf_serial_write(" stride=");serial_hex(display.stride);
 vqeaf_serial_write(" format=");serial_hex(display.format);
 vqeaf_serial_write(display.portrait?" portrait\r\n":"\r\n");
 return 0;
unavailable:
 info->framebuffer_base=0;info->framebuffer_size=0;
 info->framebuffer_width=info->framebuffer_height=info->framebuffer_stride=0;
 info->framebuffer_format=VQEAF_FB_FORMAT_NONE;
 return -1;
}
/* Terminal-style boot log helpers. Use the embedded VGA 8x8 font with its
 * real 8 px cell: the Ubuntu atlas has a 12 px advance but narrow ink, which
 * made scaled boot text look letter-spaced. */
#define TERM_CELL 8u
#define TERM_CELLS 40u
#define TERM_SCALE 2u
static void term_text(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                      const char *s,uint32_t scale,uint32_t color){
 for(uint32_t i=0;s[i];i++){
  unsigned char ch=(unsigned char)s[i];
  const uint8_t *g=vqeaf_font8x8[ch<128u?ch:(unsigned char)'?'];
  for(uint32_t row=0;row<8u;row++)
   for(uint32_t col=0;col<8u;col++)
    if(g[row]&(1u<<col))
     vqeaf_gfx_rect(fb,x+(i*8u+col)*scale,y+row*scale,scale,scale,color);
 }
}
static uint32_t term_width(const char *s,uint32_t scale){
 uint32_t n=0;while(s[n])n++;return n*TERM_CELL*scale;
}
static void term_centered(const VqeafFramebuffer *fb,uint32_t y,
                          const char *s,uint32_t scale,uint32_t color){
 uint32_t w=term_width(s,scale);
 term_text(fb,w<fb->width?(fb->width-w)/2u:0,y,s,scale,color);
}
static void term_copy(char *dst,uint32_t cap,const char *src){
 uint32_t i=0;while(src[i]&&i+1u<cap){dst[i]=src[i];i++;}dst[i]=0;
}
static void term_append(char *dst,uint32_t cap,const char *src){
 uint32_t n=0;while(dst[n])n++;
 while(src[0]&&n+1u<cap)dst[n++]=*src++;
 dst[n]=0;
}
static void term_dec(char *dst,uint32_t cap,uint32_t value){
 char tmp[12];uint32_t n=0;
 do{tmp[n++]=(char)('0'+value%10u);value/=10u;}while(value);
 uint32_t keep=n<cap-1u?n:cap-1u;
 for(uint32_t i=0;i<keep;i++)dst[i]=tmp[n-1u-i];
 dst[keep]=0;
}
static void term_box_edge(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                          uint32_t color){
 char c[2]={'+',0};term_text(fb,x,y,c,TERM_SCALE,color);
 for(uint32_t i=1u;i<TERM_CELLS-1u;i++)
  term_text(fb,x+i*TERM_CELL*TERM_SCALE,y,"-",TERM_SCALE,color);
 term_text(fb,x+(TERM_CELLS-1u)*TERM_CELL*TERM_SCALE,y,c,TERM_SCALE,color);
}
static void term_box_row(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                         const char *text,uint32_t color,uint32_t dim){
 char c[2]={'|',0};
 term_text(fb,x,y,c,TERM_SCALE,dim);
 term_text(fb,x+TERM_CELL*TERM_SCALE,y,text,TERM_SCALE,color);
 term_text(fb,x+(TERM_CELLS-1u)*TERM_CELL*TERM_SCALE,y,c,TERM_SCALE,dim);
}
static void term_progress(const VqeafFramebuffer *fb,uint32_t x,uint32_t y,
                          uint32_t percent,uint32_t color){
 char line[64],num[12];
 term_copy(line,sizeof(line),"[");
 for(uint32_t i=0;i<18u;i++)term_append(line,sizeof(line),
                                        i<percent*18u/100u?"#":"-");
 term_append(line,sizeof(line),"] ");
 term_dec(num,sizeof(num),percent);term_append(line,sizeof(line),num);
 term_append(line,sizeof(line),"%");
 term_text(fb,x,y,line,3u,color);
}
static void splash_draw(const VqeafFramebuffer *fb,uint32_t percent){
 uint32_t bg=vqeaf_gfx_rgb(fb,0x050A0B),scan=vqeaf_gfx_rgb(fb,0x0A1716);
 uint32_t accent=vqeaf_gfx_rgb(fb,0x35E0C0),text=vqeaf_gfx_rgb(fb,0xD8F5EC);
 uint32_t dim=vqeaf_gfx_rgb(fb,0x5E8F85);
 vqeaf_gfx_clear(fb,bg);
 /* Faint CRT scanlines and the two terminal rails. */
 for(uint32_t sy=0;sy<fb->height;sy+=4u)
  vqeaf_gfx_rect(fb,0,sy,fb->width,1,scan);
 vqeaf_gfx_rect(fb,0,0,fb->width,3,accent);
 vqeaf_gfx_rect(fb,0,fb->height-3,fb->width,3,accent);
 uint32_t oy=fb->height>800u?(fb->height-800u)/2u:0u;
 /* 40 cells x 8 px x 2 = 640 px, centred on the 720 px portrait panel. */
 uint32_t box_w=TERM_CELLS*TERM_CELL*TERM_SCALE;
 uint32_t x=fb->width>box_w?(fb->width-box_w)/2u:0u;

 term_centered(fb,150+oy,"QEAFIVELS OS",5u,accent);
 term_centered(fb,205+oy,"VQEAF BOOT TERMINAL 0.3",2u,dim);

 char row0[64],row1[64],row2[64],row3[64],num[12];
 term_copy(row0,sizeof(row0),"[  OK  ] GOP FRAMEBUFFER ");
 term_dec(num,sizeof(num),fb->width);term_append(row0,sizeof(row0),num);
 term_append(row0,sizeof(row0),"x");
 term_dec(num,sizeof(num),fb->height);term_append(row0,sizeof(row0),num);
 if(percent<50u){
  term_copy(row1,sizeof(row1),"[  ..  ] UEFI BOOT SERVICES READY");
  term_copy(row2,sizeof(row2),"[  ..  ] BOOT PROTOCOL V3 PENDING");
  term_copy(row3,sizeof(row3),"[ ---- ] HANDOFF PENDING");
 }else if(percent<100u){
  term_copy(row1,sizeof(row1),"[  OK  ] BOOT PROTOCOL V3 ARMED");
  term_copy(row2,sizeof(row2),"[ WAIT ] OPEN \\EFI\\VQEAF\\KERNEL.EFI");
  term_copy(row3,sizeof(row3),"[  ..  ] LOADING NATIVE KERNEL");
 }else{
  term_copy(row1,sizeof(row1),"[  OK  ] KERNEL.EFI IMAGE READ");
  term_copy(row2,sizeof(row2),"[  OK  ] NATIVE IMAGE LOADED");
  term_copy(row3,sizeof(row3),"[  OK  ] HANDOFF V3 READY");
 }
 term_box_edge(fb,x,270+oy,dim);
 term_box_row(fb,x,292+oy,row0,accent,dim);
 term_box_row(fb,x,314+oy,row1,text,dim);
 term_box_row(fb,x,336+oy,row2,text,dim);
 term_box_row(fb,x,358+oy,row3,text,dim);
 term_box_edge(fb,x,380+oy,dim);

 term_progress(fb,x,465+oy,percent,accent);
 const char *stage=percent<50u?"BOOT: INITIALIZING DISPLAY":
                   (percent<100u?"BOOT: LOADING KERNEL.EFI":
                                  "BOOT: TRANSFERRING CONTROL");
 term_centered(fb,525+oy,stage,2u,text);
 uint32_t sw=term_width(stage,2u);
 vqeaf_gfx_rect(fb,(fb->width-sw)/2u+sw+10u,525+oy,16u,16u,accent);

 vqeaf_gfx_rect(fb,fb->width/2u-120u,640+oy,240u,1u,dim);
 term_centered(fb,660+oy,"QEAFIVELS OS  |  BOOT PROTOCOL V3  |  GOP",1u,dim);
 term_centered(fb,fb->height-28,"BOOT> _",2u,accent);
}
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE*st){
 EFI_LOADED_IMAGE_PROTOCOL*parent=0,*child=0;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*fs=0;EFI_FILE_PROTOCOL*root=0,*file=0;EFI_HANDLE kernel=0;void*buffer=0;UINTN size=16*1024*1024;EFI_STATUS s;
 VqeafFramebuffer fb;int have_gop;
 VqeafBootInfoV3 info={{'V','Q','E','A','F','B','T','0'},VQEAF_BOOT_PROTOCOL_VERSION,sizeof(VqeafBootInfoV3),VQEAF_ARCH_X86_64,VQEAF_BOOT_MODE_NORMAL,0,st,image,0,0,0,0,0,0,0,0,0,0,VQEAF_FB_FORMAT_NONE};
 vqeaf_serial_init();vqeaf_serial_write(VQEAF_OS_STRING "\r\n");st->ConOut->OutputString(st->ConOut,title);st->ConOut->OutputString(st->ConOut,loading);
 have_gop=(splash_init(st,&info,&fb)==0);
 if(have_gop){splash_draw(&fb,20);splash_delay(st,250000);splash_draw(&fb,70);
  vqeaf_serial_write("SPLASH: terminal boot log ready\r\n");
  splash_delay(st,150000);}
 s=((EFI_HANDLE_PROTOCOL)BS(st)->HandleProtocol)(image,&loaded_guid,(void**)&parent);if(EFI_ERROR(s)||!parent)goto fail;
 s=((EFI_HANDLE_PROTOCOL)BS(st)->HandleProtocol)(parent->DeviceHandle,&fs_guid,(void**)&fs);if(EFI_ERROR(s)||!fs)goto fail;
 s=fs->OpenVolume(fs,&root);if(EFI_ERROR(s)||!root)goto fail;s=root->Open(root,&file,path,EFI_FILE_MODE_READ,0);if(EFI_ERROR(s)||!file)goto fail;
 s=((EFI_ALLOCATE_POOL)BS(st)->AllocatePool)(EFI_LOADER_DATA,size,&buffer);if(EFI_ERROR(s)||!buffer)goto fail;
 s=file->Read(file,&size,buffer);file->Close(file);root->Close(root);if(EFI_ERROR(s)||!size)goto fail;
 s=((EFI_LOAD_IMAGE)BS(st)->LoadImage)(0,image,0,buffer,size,&kernel);if(EFI_ERROR(s)||!kernel)goto fail;
 s=((EFI_HANDLE_PROTOCOL)BS(st)->HandleProtocol)(kernel,&loaded_guid,(void**)&child);if(EFI_ERROR(s)||!child)goto fail;
 child->LoadOptions=&info;child->LoadOptionsSize=sizeof(info);vqeaf_serial_write("Starting KERNEL.EFI with boot protocol v3\r\n");
 if(have_gop)splash_draw(&fb,100);
 s=((EFI_START_IMAGE)BS(st)->StartImage)(kernel,0,0);st->ConOut->OutputString(st->ConOut,returned);vqeaf_serial_write("Kernel returned to boot manager\r\n");return s;
fail:st->ConOut->OutputString(st->ConOut,failed);vqeaf_serial_write("BOOT FAILED\r\n");return EFI_LOAD_ERROR;
}
