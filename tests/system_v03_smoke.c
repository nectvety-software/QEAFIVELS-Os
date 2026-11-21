#include <assert.h>
#include <string.h>
#include "../system/core/include/vqeaf/block.h"
#include "../system/core/include/vqeaf/partition.h"
#include "../system/core/include/vqeaf/fs_fat16.h"
#include "../system/core/include/vqeaf/power.h"
#include "../system/core/include/vqeaf/discovery.h"
#include "../system/core/include/vqeaf/capability.h"
#include "../system/core/include/vqeaf/driver.h"
#include "../system/core/include/vqeaf/text.h"
#include "../system/core/include/vqeaf/service.h"
#include "../system/core/include/vqeaf/input.h"
#include "../system/core/include/vqeaf/power_menu.h"
#include "../system/core/include/vqeaf/storage.h"

/* ---- ramdisk block device: read-write, 512-byte sectors ---- */
#define DISK_SECTORS 8192u
#define VOL_BASE 8u
#define DATA_LBA (VOL_BASE+1u+2u+1u) /* reserved 1 + FAT 2x1 + root 1 */
static uint8_t disk[DISK_SECTORS][512];
static int rd_read(struct VqeafBlockDevice *d,uint64_t lba,uint32_t n,void *out){
 (void)d;memcpy(out,disk+lba,(size_t)n*512u);return 0;
}
static int rd_write(struct VqeafBlockDevice *d,uint64_t lba,uint32_t n,const void *in){
 (void)d;memcpy(disk+lba,in,(size_t)n*512u);return 0;
}
static const uint16_t FAT_EOC=0xFFFFu,FAT_FIRST=0xFFF8u;
static void wr16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void wr64(uint8_t *p,uint64_t v){wr32(p,(uint32_t)v);wr32(p+4,(uint32_t)(v>>32));}
static void add_dir_entry(uint8_t *sec,unsigned slot,const char *name,uint8_t attr,uint32_t cluster,uint32_t size){
 uint8_t *e=sec+slot*32;
 memset(e,0,32);
 memset(e,' ',11);
 int oi=0;
 for(const char *c=name;*c;c++){
  if(*c=='.'){oi=8;continue;}
  e[oi++]=(uint8_t)(*c>='a'&&*c<='z'?*c-'a'+'A':*c);
 }
 e[11]=attr;wr16(e+26,(uint16_t)cluster);wr32(e+28,size);
}
static void build_fat16(void){
 memset(disk,0,sizeof(disk));
 uint8_t *bpb=disk[VOL_BASE];
 bpb[0]='V';bpb[1]='Q';bpb[2]='F';bpb[3]='1';
 wr16(bpb+11,512);bpb[13]=1;wr16(bpb+14,1);bpb[16]=2;wr16(bpb+17,16);
 wr16(bpb+19,8184);bpb[21]=0xF8;wr16(bpb+22,1);
 wr16(bpb+510,0xAA55);
 for(unsigned f=0;f<2;f++){
  uint8_t *fat=disk[VOL_BASE+1+f];
  wr16(fat+0,FAT_FIRST);wr16(fat+2,FAT_EOC); /* cluster 0 media, cluster 1 EOC */
  wr16(fat+4,FAT_EOC); /* cluster 2: HELLO.TXT */
  wr16(fat+6,FAT_EOC); /* cluster 3: SYSTEM dir */
  wr16(fat+8,FAT_EOC); /* cluster 4: LANG.TXT */
 }
 uint8_t *root=disk[VOL_BASE+3];
 add_dir_entry(root,0,"HELLO.TXT",0x20,2,16);
 add_dir_entry(root,1,"SYSTEM",0x10,3,0);
 memcpy(disk[DATA_LBA],"Xin chao VQEAF!\n",16);
 add_dir_entry(disk[DATA_LBA+1],0,"LANG.TXT",0x20,4,5);
 memcpy(disk[DATA_LBA+2],"vi-VN",5);
}
static VqeafDisplayPowerState blank_events[8];
static unsigned blanks;
static void blank_cb(void *ctx,VqeafDisplayPowerState s){(void)ctx;if(blanks<8)blank_events[blanks++]=s;}
static int probe_calls;
static int probe_count(void *p){(void)p;probe_calls++;return 0;}
static const VqeafDriver storage_drv={VQEAF_DRIVER_ABI,sizeof(VqeafDriver),"ram-storage",VQEAF_DEV_STORAGE,probe_count,0};
static const VqeafDiscoveredDevice fw_devs[3]={
 {VQEAF_DEV_STORAGE,"ram-storage","qemu-fw",VQEAF_DISC_OK},
 {VQEAF_DEV_DISPLAY,"qemu-display","qemu-fw",VQEAF_DISC_OK},
 {VQEAF_DEV_RADIO,"qemu-radio","qemu-fw",VQEAF_DISC_BLOCKED_FIRMWARE},
};
static int fw_enum(const char *bus,VqeafDiscoveredDevice *out,uint32_t max){
 (void)bus;if(max<3)return -1;memcpy(out,fw_devs,sizeof(fw_devs));return 3;
}
static int fs_start(void *c){(void)c;return 0;}
static int fs_stop(void *c){(void)c;return 0;}
static int pw_start(void *c){(void)c;return 0;}
static int pw_stop(void *c){(void)c;return 0;}
int main(void){
 int _r;
 /* ---- 1. block devices ---- */
 vqeaf_block_reset();
 static struct VqeafBlockDevice ram,ro;
 static const char ram_name[]="ram0",ro_name[]="ro0";
 ram.name=ram_name;ram.sector_size=512;ram.sector_count=DISK_SECTORS;
 ram.read_sectors=rd_read;ram.write_sectors=rd_write;
 _r = (int)(vqeaf_block_register(&ram)==0);
 assert(_r);
 _r = (int)(vqeaf_block_register(&ram)==-3);
 assert(_r);
 ro.name=ro_name;ro.sector_size=512;ro.sector_count=4;ro.read_sectors=rd_read;ro.write_sectors=0;
 _r = (int)(vqeaf_block_register(&ro)==1);
 assert(_r);
 uint8_t buf[512];
 _r = (int)(vqeaf_block_read(0,0,1,buf)==0);
 assert(_r);
 _r = (int)(vqeaf_block_read(0,DISK_SECTORS,1,buf)==-2);
 assert(_r);
 _r = (int)(vqeaf_block_read(0,DISK_SECTORS-1,2,buf)==-2);
 assert(_r);
 _r = (int)(vqeaf_block_write(1,0,1,buf)==-3);
 assert(_r);
 _r = (int)(vqeaf_block_write(0,0,1,buf)==0);
 assert(_r);

 /* ---- 2. partition parsing ---- */
 memset(disk,0,512*4);
 wr16(disk[0]+510,0xAA55);
 disk[0][446+4]=0x06;wr32(disk[0]+446+8,8);wr32(disk[0]+446+12,16);
 disk[0][446+16+4]=0x06;wr32(disk[0]+446+16+8,24);wr32(disk[0]+446+16+12,8);
 VqeafPartition parts[4];
 _r = (int)(vqeaf_mbr_parse(0,parts,4)==2);
 assert(_r);
 assert(parts[0].start_lba==8&&parts[0].sector_count==16&&parts[0].type==0x06);
 assert(parts[1].start_lba==24&&parts[1].mbr_index==1);
 wr32(disk[0]+446+16+8,16);wr32(disk[0]+446+16+12,32); /* second overlaps first */
 _r = (int)(vqeaf_mbr_parse(0,parts,4)==-5);
 assert(_r);
 wr32(disk[0]+446+16+12,9999); /* past disk end */
 _r = (int)(vqeaf_mbr_parse(0,parts,4)==-4);
 assert(_r);
 disk[0][510]=0;
 _r = (int)(vqeaf_mbr_parse(0,parts,4)==-3);
 assert(_r);
 build_fat16();
 disk[0][446+4]=0xEE; /* protective MBR */
 wr16(disk[0]+510,0xAA55);
 _r = (int)(vqeaf_gpt_present(0)==1);
 assert(_r);
 disk[0][446+4]=0x06;assert(vqeaf_gpt_present(0)==0);
 build_fat16();

 /* ---- 3. FAT16 mount + read ---- */
 VqeafFat16Mount mnt;
 _r = (int)(vqeaf_fat16_mount(0,VOL_BASE,&mnt)==0);
 assert(_r);
 assert(mnt.fs.cluster_count>=4085&&mnt.fs.cluster_count<65525);
 VqeafVnode *f=0;
 char text[64];
 _r = (int)(vqeaf_fat16_walk(&mnt,"/HELLO.TXT",&f)==0&&f->size==16);
 assert(_r);
 memset(text,0,sizeof(text));
 _r = (int)(vqeaf_vfs_read(f,0,text,16)==16&&memcmp(text,"Xin chao VQEAF!\n",16)==0);
 assert(_r);
 memset(text,0,sizeof(text));
 _r = (int)(vqeaf_vfs_read(f,8,text,8)==8&&memcmp(text," VQEAF!\n",8)==0);
 assert(_r);
 _r = (int)(vqeaf_fat16_walk(&mnt,"/SYSTEM/LANG.TXT",&f)==0);
 assert(_r);
 memset(text,0,sizeof(text));
 _r = (int)(vqeaf_vfs_read(f,0,text,5)==5&&memcmp(text,"vi-VN",5)==0);
 assert(_r);
 _r = (int)(vqeaf_fat16_walk(&mnt,"/SYSTEM",&f)==0&&f->type==1);
 assert(_r);
 _r = (int)(vqeaf_fat16_walk(&mnt,"/missing.txt",&f)==-3);
 assert(_r);
 _r = (int)(vqeaf_fat16_walk(&mnt,"/HELLO..TXT",&f)==-3);
 assert(_r);
 _r = (int)(vqeaf_fat16_walk(&mnt,"relative",&f)==-1);
 assert(_r);
 _r = (int)(vqeaf_fat16_walk(&mnt,"/SYSTEM/LANG.TXT/",&f)==0);
 assert(_r);
 uint16_t saved_total16=*(uint16_t*)(disk[VOL_BASE]+19);
 *(uint16_t*)(disk[VOL_BASE]+19)=0; /* force the 32-bit total field */
 *(uint32_t*)(disk[VOL_BASE]+36)=1024; /* FAT12-range volume */
 _r = (int)(vqeaf_fat16_mount(0,VOL_BASE,&mnt)==-6);
 assert(_r);
 *(uint16_t*)(disk[VOL_BASE]+19)=saved_total16;
 build_fat16();
 _r = (int)(vqeaf_fat16_mount(0,0,&mnt)==-3);
 assert(_r);

 /* ---- 4. power v0.3 ---- */
 blanks=0;
 VqeafPowerManager pm;
 _r = (int)(vqeaf_power_init(&pm,VQEAF_TIMEOUT_15S,1,0)==0);
 assert(_r);
 _r = (int)(vqeaf_power_set_dim_stage(&pm,5000)==0);
 assert(_r);
 _r = (int)(vqeaf_power_set_blank_callback(&pm,blank_cb,0)==0);
 assert(_r);
 vqeaf_power_user_activity(&pm,1000);
 vqeaf_power_tick(&pm,11000);assert(pm.state==VQEAF_POWER_DIMMED);
 vqeaf_power_tick(&pm,16000);assert(pm.state==VQEAF_POWER_DISPLAY_OFF);
 assert(blanks==2&&blank_events[0]==VQEAF_POWER_DIMMED&&blank_events[1]==VQEAF_POWER_DISPLAY_OFF);
 vqeaf_power_user_activity(&pm,17000);assert(pm.state==VQEAF_POWER_ACTIVE);
 _r = (int)(vqeaf_power_wake_lock(&pm,"navigation",17000,0)==0);
 assert(_r);
 vqeaf_power_tick(&pm,999999);assert(pm.state==VQEAF_POWER_ACTIVE);
 _r = (int)(vqeaf_power_wake_active(&pm)==1);
 assert(_r);
 _r = (int)(vqeaf_power_wake_release(&pm,"navigation",999999)==0);
 assert(_r);
 vqeaf_power_tick(&pm,1000000);assert(pm.state!=VQEAF_POWER_ACTIVE);
 vqeaf_power_user_activity(&pm,1001000);
 _r = (int)(vqeaf_power_wake_lock(&pm,"ota",1001000,60000)==0);
 assert(_r);
 vqeaf_power_tick(&pm,1060999);assert(pm.state==VQEAF_POWER_ACTIVE); /* TTL still valid */
 vqeaf_power_tick(&pm,1062000);assert(pm.state==VQEAF_POWER_DISPLAY_OFF);
 assert(pm.battery_present==0); /* no provider = absent, no invented data */
 _r = (int)(vqeaf_power_set_dim_stage(&pm,0)==0);
 assert(_r);
 VqeafBatteryStatus bat;memset(&bat,0,sizeof(bat));
 bat.present=1;bat.state=VQEAF_BATTERY_DISCHARGING;bat.level_permille=1000;
 vqeaf_power_user_activity(&pm,1100000);
 vqeaf_power_set_battery(&pm,&bat);
 vqeaf_power_tick(&pm,1100000+14999);assert(pm.state==VQEAF_POWER_ACTIVE);
 bat.level_permille=100;
 vqeaf_power_set_battery(&pm,&bat);
 vqeaf_power_tick(&pm,1100000+7600);assert(pm.state==VQEAF_POWER_DISPLAY_OFF); /* 10% halves timeout */
 vqeaf_power_user_activity(&pm,1200000);
 bat.level_permille=40;
 vqeaf_power_set_battery(&pm,&bat);
 vqeaf_power_tick(&pm,1200000);assert(pm.state==VQEAF_POWER_DISPLAY_OFF); /* critical: immediate */
 bat.level_permille=1500;
 vqeaf_power_set_battery(&pm,&bat);
 assert(pm.level_permille==40); /* impossible telemetry rejected */
 bat.present=0;vqeaf_power_set_battery(&pm,&bat);
 assert(pm.battery_present==0&&pm.level_permille==0);

 /* ---- 5. device discovery ---- */
 vqeaf_cap_reset();vqeaf_driver_reset();
 _r = (int)(vqeaf_driver_register(&storage_drv)==0);
 assert(_r);
 _r = (int)(vqeaf_discovery_register_bus("qemu-fw",fw_enum)==0);
 assert(_r);
 _r = (int)(vqeaf_discovery_register_bus("qemu-fw",fw_enum)==-2);
 assert(_r);
 _r = (int)(vqeaf_discovery_scan()==3);
 assert(_r);
 _r = (int)(vqeaf_discovery_device_count()==3);
 assert(_r);
 assert(probe_calls==1); /* only the device with a matching driver was probed */
 VqeafCapability cap;
 _r = (int)(vqeaf_cap_get("dev.qemu-fw/ram-storage",&cap)==0&&cap.state==VQEAF_CAP_READY);
 assert(_r);
 _r = (int)(vqeaf_cap_get("dev.qemu-fw/qemu-display",&cap)==0&&cap.state==VQEAF_CAP_PRESENT);
 assert(_r);
 _r = (int)(vqeaf_cap_get("dev.qemu-fw/qemu-radio",&cap)==0&&cap.state==VQEAF_CAP_BLOCKED_FIRMWARE);
 assert(_r);

 /* ---- 6. text v0.3 ---- */
 uint32_t cp=0;int len=0;
 _r = (int)(vqeaf_utf8_next("\xC3\xA9",&cp)==2&&cp==0xE9);
 assert(_r);
 _r = (int)(vqeaf_utf8_next("\xC3",&cp)==0);
 assert(_r);
 _r = (int)(vqeaf_utf8_next("\xC0\x80",&cp)==0);
 assert(_r);
 _r = (int)(vqeaf_utf8_next("\xED\xA0\x80",&cp)==0);
 assert(_r);
 int composed=0,marks=0;
 _r = (int)(vqeaf_text_next_grapheme("e\xCC\x82",&cp,&composed,&len,&marks)==1&&cp==0xEA&&composed==1&&marks==1);
 assert(_r);
 _r = (int)(vqeaf_text_next_grapheme("Ệ",&cp,&composed,&len,&marks)==1&&cp==0x1EC6&&marks==1);
 assert(_r);
 _r = (int)(vqeaf_text_next_grapheme("\xC6\xA0\xCC\x81",&cp,&composed,&len,&marks)==1&&cp==0x1EDA);
 assert(_r);
 uint32_t w1=0,g1=0,w2=0,g2=0;
 _r = (int)(vqeaf_text_measure("Tiếng Việt",&w1,&g1)==0);
 assert(_r);
 _r = (int)(vqeaf_text_measure("Tiê\xCC\x82ng Vi\xC3\xAA\xCC\xA3t",&w2,&g2)==0);
 assert(_r);
 assert(w1==w2&&g1==g2&&g1==10); /* composed == decomposed, 10 graphemes */
 _r = (int)(vqeaf_text_advance(0x0301)==0);
 assert(_r);
 _r = (int)(vqeaf_text_glyph_fallback(0x1EBF)==0x1EBF);
 assert(_r);
 _r = (int)(vqeaf_text_glyph_fallback(0x1E25)==0xFFFD);
 assert(_r);
 _r = (int)(vqeaf_text_glyph_fallback(0x4E2D)==0xFFFD);
 assert(_r);
 _r = (int)(vqeaf_text_measure("\xFF\xFE",&w1,&g1)==-2);
 assert(_r);

 /* ---- 7. service manager ---- */
 vqeaf_svc_reset();
 _r = (int)(vqeaf_svc_register(0)==-1);
 assert(_r);
 VqeafServiceDef fs_def;memset(&fs_def,0,sizeof(fs_def));
 fs_def.name="storage.fs";fs_def.version=1;fs_def.start=fs_start;fs_def.stop=fs_stop;
 fs_def.permissions[0]="storage.mount";
 _r = (int)(vqeaf_svc_register(&fs_def)==0);
 assert(_r);
 VqeafServiceDef pw_def;memset(&pw_def,0,sizeof(pw_def));
 pw_def.name="power.ui";pw_def.version=1;pw_def.start=pw_start;pw_def.stop=pw_stop;
 pw_def.dependencies[0]="storage.fs";
 pw_def.permissions[0]="storage.mount";
 _r = (int)(vqeaf_svc_register(&pw_def)==1);
 assert(_r);
 _r = (int)(vqeaf_svc_register(&pw_def)==-4);
 assert(_r);
 VqeafServiceDef orphan;memset(&orphan,0,sizeof(orphan));
 orphan.name="orphan";orphan.version=1;orphan.start=fs_start;orphan.stop=fs_stop;
 orphan.dependencies[0]="ghost";
 _r = (int)(vqeaf_svc_register(&orphan)==-5);
 assert(_r);
 _r = (int)(vqeaf_svc_find("storage.fs")==0);
 assert(_r);
 _r = (int)(vqeaf_svc_holds(0,"storage.mount")==0);
 assert(_r);
 _r = (int)(vqeaf_svc_start(1,0)==-4);
 assert(_r);
 _r = (int)(vqeaf_svc_grant(0,"storage.mount")==0);
 assert(_r);
 _r = (int)(vqeaf_svc_start(1,0)==0);
 assert(_r);
 _r = (int)(vqeaf_svc_state(0)==VQEAF_SVC_RUNNING&&vqeaf_svc_state(1)==VQEAF_SVC_RUNNING);
 assert(_r);
 _r = (int)(vqeaf_svc_stop(0,0)==-4);
 assert(_r);
 _r = (int)(vqeaf_svc_stop(1,0)==0);
 assert(_r);
 _r = (int)(vqeaf_svc_state(1)==VQEAF_SVC_STOPPED);
 assert(_r);
 _r = (int)(vqeaf_svc_stop(0,0)==0);
 assert(_r);
 _r = (int)(vqeaf_svc_revoke(0,"storage.mount")==0);
 assert(_r);
 _r = (int)(vqeaf_svc_start(0,0)==-4);
 assert(_r);
 _r = (int)(vqeaf_svc_grant(0,"storage.mount")==0);
 assert(_r);
 _r = (int)(vqeaf_svc_start(0,0)==0);
 assert(_r);
 _r = (int)(vqeaf_svc_stop_all(0)==0);
 assert(_r);
 _r = (int)(vqeaf_svc_state(0)==VQEAF_SVC_STOPPED&&vqeaf_svc_state(1)==VQEAF_SVC_STOPPED);
 assert(_r);

 /* ---- 8. FAT16 write (create + overwrite) ---- */
 _r = vqeaf_fat16_mount(0,VOL_BASE,&mnt);
 assert(_r==0);
 assert(vqeaf_fat16_write_file(&mnt,"SETTINGS.DAT","boot-count=1",11)==11);
 assert(vqeaf_fat16_walk(&mnt,"/SETTINGS.DAT",&f)==0&&f->size==11);
 memset(text,0,sizeof(text));
 assert(vqeaf_vfs_read(f,0,text,11)==11&&memcmp(text,"boot-count=1",11)==0);
 assert(vqeaf_fat16_write_file(&mnt,"SETTINGS.DAT","ok",2)==2); /* shrink overwrite */
 assert(vqeaf_fat16_walk(&mnt,"/SETTINGS.DAT",&f)==0&&f->size==2);
 memset(text,0,sizeof(text));
 assert(vqeaf_vfs_read(f,0,text,2)==2&&memcmp(text,"ok",2)==0);
 assert(vqeaf_fat16_write_file(&mnt,"SETTINGS.DAT","longer-than-ok",15)==-4); /* no growth yet */
 assert(vqeaf_fat16_write_file(&mnt,"BAD..NAME","x",1)==-2); /* invalid 8.3 */

 /* ---- 9. persistence: fresh mount from the same device == reboot ---- */
 assert(vqeaf_fat16_write_file(&mnt,"DATA.TXT","persistent",10)==10);
 memset(&mnt,0,sizeof(mnt));
 _r = vqeaf_fat16_mount(0,VOL_BASE,&mnt);
 assert(_r==0);
 assert(vqeaf_fat16_walk(&mnt,"/DATA.TXT",&f)==0);
 memset(text,0,sizeof(text));
 assert(vqeaf_vfs_read(f,0,text,10)==10&&memcmp(text,"persistent",10)==0);

 /* ---- 10. GPT parsing ---- */
 memset(disk,0,512*8);
 memcpy(disk[1],"EFI PART",8);
 wr32(disk[1]+12,92);            /* header size */
 wr64(disk[1]+72,2);             /* partition entries at LBA 2 */
 wr32(disk[1]+80,2);             /* 2 entries */
 wr32(disk[1]+84,128);           /* entry size */
 uint8_t *e0=disk[2];
 for(int i=0;i<8;i++)e0[i]=(uint8_t)(0xA1+i); /* type GUID present */
 wr64(e0+32,2048);wr64(e0+40,4095);
 uint8_t *e1=disk[2]+128;
 for(int i=0;i<8;i++)e1[i]=(uint8_t)(0xB2+i);
 wr64(e1+32,4096);wr64(e1+40,8191);
 VqeafPartition gpts[4];
 assert(vqeaf_gpt_parse(0,gpts,4)==2);
 assert(gpts[0].start_lba==2048&&gpts[0].sector_count==2048);
 assert(gpts[1].start_lba==4096&&gpts[1].sector_count==4096);
 wr64(e1+32,4095);assert(vqeaf_gpt_parse(0,gpts,4)==-9);   /* overlap */
 wr64(e1+32,999999);assert(vqeaf_gpt_parse(0,gpts,4)==-8); /* past disk end */
 memcpy(disk[1],"NOPE",4);assert(vqeaf_gpt_parse(0,gpts,4)==-3);
 build_fat16();

 /* ---- 11. /data persistent mount via storage service ---- */
 vqeaf_storage_reset();
 _r = vqeaf_storage_mount_fat16(0,VOL_BASE,"/data",0);
 assert(_r==0);
 VqeafVnode *dataroot=0;const char *rel=0;
 assert(vqeaf_storage_resolve("/data/HELLO.TXT",&dataroot,&rel)==0);
 assert(dataroot!=0&&strcmp(rel,"/HELLO.TXT")==0);
 assert(vqeaf_fat16_walk(&mnt,rel,&f)==0&&f->size==16);
 assert(vqeaf_storage_unmount("/data")==0);
 assert(vqeaf_storage_resolve("/data/HELLO.TXT",&dataroot,&rel)==-2);
 assert(vqeaf_storage_mount_fat16(0,VOL_BASE,"/data",0)==0);
 assert(vqeaf_storage_mount_fat16(0,VOL_BASE,"/data",0)==-5); /* refuses shadowing */

 /* ---- 12. input queue + power menu modal ---- */
 vqeaf_input_reset();
 VqeafInputEvent ev={VQEAF_KEY_VOLUME_UP,VQEAF_KEY_TAP};
 assert(vqeaf_input_push(&ev)==0);
 assert(vqeaf_input_push(0)==-1);
 assert(vqeaf_input_pending()==1);
 VqeafInputEvent got;
 assert(vqeaf_input_pop(&got)==0&&got.code==VQEAF_KEY_VOLUME_UP&&got.gesture==VQEAF_KEY_TAP);
 assert(vqeaf_input_pop(&got)==-2);
 VqeafPowerMenu menu;vqeaf_pmenu_init(&menu);
 assert(menu.state==VQEAF_PMENU_CLOSED);
 VqeafInputEvent pwr={VQEAF_KEY_POWER,VQEAF_KEY_LONG_PRESS};
 assert(vqeaf_pmenu_handle(&menu,&pwr)==VQEAF_PMENU_NONE&&menu.state==VQEAF_PMENU_OPEN&&menu.selected==0);
 VqeafInputEvent vdown={VQEAF_KEY_VOLUME_DOWN,VQEAF_KEY_TAP};
 assert(vqeaf_pmenu_handle(&menu,&vdown)==VQEAF_PMENU_NONE&&menu.selected==1);
 assert(vqeaf_pmenu_handle(&menu,&vdown)==VQEAF_PMENU_NONE&&menu.selected==2);
 VqeafInputEvent vup={VQEAF_KEY_VOLUME_UP,VQEAF_KEY_TAP};
 assert(vqeaf_pmenu_handle(&menu,&vup)==VQEAF_PMENU_NONE&&menu.selected==1);
 assert(vqeaf_pmenu_handle(&menu,&pwr)==VQEAF_PMENU_NONE&&menu.state==VQEAF_PMENU_CLOSED); /* cancel */
 assert(vqeaf_pmenu_handle(&menu,&pwr)==VQEAF_PMENU_NONE&&menu.state==VQEAF_PMENU_OPEN);   /* reopen */
 assert(vqeaf_pmenu_handle(&menu,&vdown)==VQEAF_PMENU_NONE&&menu.selected==1);            /* Restart */
 VqeafInputEvent ptap={VQEAF_KEY_POWER,VQEAF_KEY_TAP};
 assert(vqeaf_pmenu_handle(&menu,&ptap)==VQEAF_PMENU_RESTART&&menu.state==VQEAF_PMENU_CLOSED);
 assert(vqeaf_pmenu_handle(&menu,&ptap)==VQEAF_PMENU_NONE); /* taps never open the menu */
 assert(vqeaf_pmenu_item_name(2)&&strcmp(vqeaf_pmenu_item_name(2),"Fastboot")==0);
 assert(vqeaf_pmenu_item_action(3)==VQEAF_PMENU_SLEEP&&vqeaf_pmenu_item_name(4)==0);
 return 0;
}
