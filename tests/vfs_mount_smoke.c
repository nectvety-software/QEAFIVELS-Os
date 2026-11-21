#include <assert.h>
#include <string.h>
#include "../system/core/include/vqeaf/block.h"
#include "../system/core/include/vqeaf/storage.h"
#include "../system/core/include/vqeaf/fs_fat16.h"
#include "../kernel/include/vqeaf/vfs.h"

/* Mount namespace smoke test: two independent FAT16 volumes on two block
 * devices, mounted at different paths and read through vqeaf_storage_walk.
 * A shared filesystem instance would let the second mount overwrite the first
 * volume's node pool, so the reads are deliberately interleaved. */

#define DISK_SECTORS 8192u
#define VOL_BASE 8u
#define DATA_LBA (VOL_BASE+1u+2u+1u)
static uint8_t disks[2][DISK_SECTORS][512];

static int rd_read(struct VqeafBlockDevice *d,uint64_t lba,uint32_t n,void *out){
 int i=(int)(intptr_t)d->ctx;
 if(lba>DISK_SECTORS||DISK_SECTORS-lba<n)return -1;
 memcpy(out,disks[i]+lba,(size_t)n*512u);return 0;
}
static void wr16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
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
static void build_volume(int idx,const char *hello,uint32_t hello_len,const char *lang){
 uint8_t *disk=disks[idx][0];
 memset(disks[idx],0,sizeof(disks[idx]));
 uint8_t *bpb=disk+VOL_BASE*512u;
 bpb[0]='V';bpb[1]='Q';bpb[2]='F';bpb[3]=(uint8_t)('1'+idx);
 wr16(bpb+11,512);bpb[13]=1;wr16(bpb+14,1);bpb[16]=2;wr16(bpb+17,16);
 wr16(bpb+19,8184);bpb[21]=0xF8;wr16(bpb+22,1);
 wr16(bpb+510,0xAA55);
 for(unsigned f=0;f<2;f++){
  uint8_t *fat=disk+(VOL_BASE+1+f)*512u;
  wr16(fat+0,0xFFF8u);wr16(fat+2,0xFFFFu);
  wr16(fat+4,0xFFFFu); /* cluster 2: HELLO.TXT */
  wr16(fat+6,0xFFFFu); /* cluster 3: SYSTEM dir */
  wr16(fat+8,0xFFFFu); /* cluster 4: LANG.TXT */
 }
 uint8_t *root=disk+(VOL_BASE+3)*512u;
 add_dir_entry(root,0,"HELLO.TXT",0x20,2,hello_len);
 add_dir_entry(root,1,"SYSTEM",0x10,3,0);
 memcpy(disk+DATA_LBA*512u,hello,hello_len);
 add_dir_entry(disk+(DATA_LBA+1)*512u,0,"LANG.TXT",0x20,4,5);
 memcpy(disk+(DATA_LBA+2)*512u,lang,5);
}
static void read_file(const char *path,char *out,uint32_t cap){
 VqeafVnode *node=0;
 int r=vqeaf_storage_walk(path,&node);
 assert(r==0&&node);
 int64_t got=vqeaf_vfs_read(node,0,out,cap-1u);
 assert(got>=0);
 out[got]=0;
}
int main(void){
 build_volume(0,"Xin chao VQEAF!\n",16,"vi-VN");
 build_volume(1,"Second volume B!\n",17,"en-US");
 static struct VqeafBlockDevice dev_a={"fat16-a",512u,DISK_SECTORS,rd_read,0,(void*)(intptr_t)0};
 static struct VqeafBlockDevice dev_b={"fat16-b",512u,DISK_SECTORS,rd_read,0,(void*)(intptr_t)1};
 vqeaf_block_reset();
 assert(vqeaf_block_register(&dev_a)==0);
 assert(vqeaf_block_register(&dev_b)==1);
 vqeaf_storage_reset();

 /* ---- two volumes mounted at once stay independent ---- */
 assert(vqeaf_storage_mount_fat16(0,VOL_BASE,"/disk",0)==0);
 char a1[32];read_file("/disk/HELLO.TXT",a1,sizeof a1);
 VqeafVnode *file_a=0;
 assert(vqeaf_storage_walk("/disk/HELLO.TXT",&file_a)==0&&file_a->size==16);
 assert(vqeaf_storage_mount_fat16(1,VOL_BASE,"/data",0)==0);
 char b1[32];read_file("/data/HELLO.TXT",b1,sizeof b1);
 assert(strcmp(a1,"Xin chao VQEAF!\n")==0);
 assert(strcmp(b1,"Second volume B!\n")==0);
 /* The first volume's node must survive the second mount: the old shared
  * static instance would have made this read return the B volume bytes. */
 char a2[32];memset(a2,0,sizeof a2);
 int64_t again=vqeaf_vfs_read(file_a,0,a2,sizeof a2-1u);
 assert(again==16&&strcmp(a2,"Xin chao VQEAF!\n")==0);
 char lang_a[8],lang_b[8];
 read_file("/disk/SYSTEM/LANG.TXT",lang_a,sizeof lang_a);
 read_file("/data/SYSTEM/LANG.TXT",lang_b,sizeof lang_b);
 assert(strcmp(lang_a,"vi-VN")==0&&strcmp(lang_b,"en-US")==0);
 VqeafVnode *root_a=0,*root_b=0;const char *rel=0;
 assert(vqeaf_storage_resolve("/disk",&root_a,&rel)==0&&rel[0]==0);
 assert(vqeaf_storage_resolve("/data",&root_b,&rel)==0);
 assert(root_a&&root_b&&root_a!=root_b);
 VqeafVnode *missing=0;
 assert(vqeaf_storage_walk("/disk/NOPE.TXT",&missing)!=0);

 /* ---- unmount invalidates, remount restores, duplicates rejected ---- */
 assert(vqeaf_storage_unmount("/disk")==0);
 VqeafVnode *n=0;
 assert(vqeaf_storage_walk("/disk/HELLO.TXT",&n)==-2);
 assert(vqeaf_storage_walk("/data/HELLO.TXT",&n)==0);
 assert(vqeaf_storage_mount_fat16(0,VOL_BASE,"/disk",0)==0);
 read_file("/disk/HELLO.TXT",a1,sizeof a1);
 assert(strcmp(a1,"Xin chao VQEAF!\n")==0);
 assert(vqeaf_storage_mount_fat16(0,VOL_BASE,"/disk",0)==-5); /* refuses shadowing */
 read_file("/disk/HELLO.TXT",a1,sizeof a1);
 assert(strcmp(a1,"Xin chao VQEAF!\n")==0);
 assert(vqeaf_storage_unmount("/nope")==-2);
 assert(vqeaf_storage_unmount(0)==-1);

 /* ---- nested mounts use the longest prefix, with path boundary ---- */
 vqeaf_storage_reset();
 assert(vqeaf_storage_mount_fat16(0,VOL_BASE,"/",0)==0);
 assert(vqeaf_storage_mount_fat16(1,VOL_BASE,"/data",0)==0);
 read_file("/HELLO.TXT",a1,sizeof a1);
 read_file("/data/HELLO.TXT",b1,sizeof b1);
 assert(strcmp(a1,"Xin chao VQEAF!\n")==0);
 assert(strcmp(b1,"Second volume B!\n")==0);
 VqeafVnode *root_slash=0;
 assert(vqeaf_storage_resolve("/",&root_slash,&rel)==0);
 assert(root_slash!=0);
 assert(vqeaf_storage_resolve("/data2/x",&n,&rel)==0&&n==root_slash); /* not /data */
 assert(vqeaf_storage_unmount("/data")==0);
 read_file("/HELLO.TXT",a1,sizeof a1);
 assert(vqeaf_storage_resolve("/data/HELLO.TXT",&n,&rel)==0); /* falls back to "/" */
 assert(vqeaf_storage_walk("/data/HELLO.TXT",&n)!=0);

 /* ---- mount table limit and a reusable FAT slot ---- */
 vqeaf_storage_reset();
 static VqeafVnode dummies[VQEAF_MOUNT_MAX+1];
 static const char *paths[VQEAF_MOUNT_MAX+1]={"/m0","/m1","/m2","/m3","/m4","/m5","/m6","/m7","/m8"};
 for(uint32_t i=0;i<VQEAF_MOUNT_MAX;i++)assert(vqeaf_storage_mount(paths[i],&dummies[i],0)==0);
 assert(vqeaf_storage_mount(paths[VQEAF_MOUNT_MAX],&dummies[VQEAF_MOUNT_MAX],0)==-3);
 assert(vqeaf_storage_mount_fat16(0,VOL_BASE,"/disk",0)==-3); /* full table, slot released */
 assert(vqeaf_storage_unmount("/m0")==0);
 assert(vqeaf_storage_mount_fat16(0,VOL_BASE,"/disk",0)==0);
 read_file("/disk/HELLO.TXT",a1,sizeof a1);
 assert(strcmp(a1,"Xin chao VQEAF!\n")==0);

 /* ---- path validation ---- */
 vqeaf_storage_reset();
 assert(vqeaf_storage_mount("/m0",&dummies[0],0)==0);
 char longp[120];
 memset(longp,'a',sizeof longp);
 longp[0]='/';longp[99]=0; /* 99 chars: over the 95-char mount slot */
 assert(vqeaf_storage_mount(longp,&dummies[1],0)==-1);
 assert(vqeaf_storage_mount("relative",&dummies[1],0)==-1);
 assert(vqeaf_storage_mount("/../escape",&dummies[1],0)==-1);
 assert(vqeaf_storage_resolve("/m0/../x",&n,&rel)==-1);
 assert(vqeaf_storage_walk("/m0/../x",&n)==-1);
 assert(vqeaf_storage_walk("/m0/file",&n)==-3); /* opaque root has no lookup */
 assert(vqeaf_storage_walk(0,&n)==-1);
 assert(vqeaf_storage_resolve("/m0/x",0,&rel)==-1);
 assert(vqeaf_storage_resolve("/m0/x",&n,0)==-1);
 return 0;
}
