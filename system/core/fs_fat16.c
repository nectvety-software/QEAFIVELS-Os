#include <string.h>
#include "include/vqeaf/fs_fat16.h"
static uint16_t rd16(const uint8_t*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t rd32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void wr16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static int pow2(uint32_t v){return v&&!(v&(v-1));}
static uint64_t cluster_lba(const VqeafFat16 *fs,uint32_t c){return fs->data_lba+(uint64_t)(c-2)*fs->sectors_per_cluster;}
FatNode *vqeaf_fat_alloc_node(VqeafFat16Mount *mnt){
 if(!mnt||mnt->node_count>=VQEAF_FAT_NODE_MAX)return 0;
 FatNode *n=&mnt->pool[mnt->node_count++];
 memset(n,0,sizeof(*n));return n;
}
static int fat_entry(const VqeafFat16 *fs,uint32_t cluster,uint32_t *next){
 if(cluster<2||cluster>=fs->cluster_count+2)return -1;
 uint64_t off=(uint64_t)cluster*2u; /* FAT16: 2 bytes per entry */
 uint64_t lba=fs->fat_lba+off/fs->bytes_per_sector;
 uint32_t in_off=(uint32_t)(off%fs->bytes_per_sector);
 uint8_t sec[VQEAF_SECTOR_MAX];
 if(vqeaf_block_read(fs->dev_id,lba,1,sec))return -3;
 *next=rd16(sec+in_off);
 return 0;
}
int vqeaf_fat16_mount(uint32_t dev_id,uint64_t base_lba,VqeafFat16Mount *out){
 if(!out)return -1;
 memset(out,0,sizeof(*out));
 uint8_t bpb[VQEAF_SECTOR_MAX];
 if(vqeaf_block_read(dev_id,base_lba,1,bpb))return -2;
 /* On-disk bytes are 55 AA; as a little-endian word that is 0xAA55. */
 if(rd16(bpb+510)!=0xAA55)return -3; /* volume boot signature */
 VqeafFat16 *fs=&out->fs;
 fs->dev_id=dev_id;fs->base_lba=base_lba;
 fs->bytes_per_sector=rd16(bpb+11);
 fs->sectors_per_cluster=bpb[13];
 fs->reserved_sectors=rd16(bpb+14);
 fs->fat_count=bpb[16];
 fs->root_entries=rd16(bpb+17);
 fs->fat_sectors=rd16(bpb+22);
 fs->total_sectors=rd16(bpb+19)?rd16(bpb+19):rd32(bpb+36);
 if(!pow2(fs->bytes_per_sector)||fs->bytes_per_sector<512||fs->bytes_per_sector>VQEAF_SECTOR_MAX)return -4;
 if(!pow2(fs->sectors_per_cluster)||!fs->sectors_per_cluster||fs->sectors_per_cluster>128)return -4;
 if(!fs->reserved_sectors||!fs->fat_count||fs->fat_count>2||!fs->fat_sectors||!fs->total_sectors||!fs->root_entries)return -4;
 uint32_t root_sectors=(fs->root_entries*32u+fs->bytes_per_sector-1)/fs->bytes_per_sector;
 uint64_t meta=(uint64_t)fs->reserved_sectors+(uint64_t)fs->fat_count*fs->fat_sectors+root_sectors;
 if((uint64_t)fs->total_sectors<meta)return -5;
 fs->cluster_count=(uint32_t)(((uint64_t)fs->total_sectors-meta)/fs->sectors_per_cluster);
 if(fs->cluster_count<4085||fs->cluster_count>=65525)return -6; /* FAT12 or FAT32: reject, never guess */
 fs->fat_lba=base_lba+fs->reserved_sectors;
 fs->root_lba=fs->fat_lba+(uint64_t)fs->fat_count*fs->fat_sectors;
 fs->data_lba=fs->root_lba+root_sectors;
 FatNode *rn=vqeaf_fat_alloc_node(out);
 if(!rn)return -7;
 rn->mnt=out;rn->first_cluster=0;rn->size=0;rn->is_dir=1;
 out->root_vnode.ops=&vqeaf_fat16_vfs_ops;
 out->root_vnode.private_data=rn;out->root_vnode.size=0;out->root_vnode.type=1;out->root_vnode.flags=0;
 return 0;
}
static void name_to_83(const char *in,uint8_t out[11],int *ok){
 int dot=0,oi=0;*ok=0;memset(out,' ',11);
 if(!in||!*in)return;
 if(in[0]=='.'&&(in[1]==0||(in[1]=='.'&&in[2]==0))){out[0]='.';if(in[1]=='.')out[1]='.';*ok=1;return;}
 for(;*in;in++){
  char c=*in;
  if(c=='.'){if(dot||oi==0)return;dot=1;oi=8;continue;}
  if(c>='a'&&c<='z')c=(char)(c-'a'+'A');
  if((c<'A'||c>'Z')&&(c<'0'||c>'9')&&c!='_'&&c!='-'&&c!='#'&&c!='$'&&c!='@'&&c!='!'&&c!='('&&c!=')')return;
  if(oi>=11)return;
  out[oi++]=(uint8_t)c;
 }
 if(!oi||(dot&&oi==8))return;
 *ok=1;
}
static int dir_lookup(const VqeafFat16 *fs,uint32_t dir_cluster,const uint8_t name83[11],uint32_t *first,uint32_t *size,int *is_dir){
 uint8_t sec[VQEAF_SECTOR_MAX];
 uint32_t cluster=dir_cluster;
 uint64_t lba;uint64_t entries_left;
 if(dir_cluster==0){lba=fs->root_lba;entries_left=(uint64_t)fs->root_entries;}
 else{lba=cluster_lba(fs,dir_cluster);entries_left=(uint64_t)fs->sectors_per_cluster*fs->bytes_per_sector/32u;}
 uint32_t sector_in_cluster=0;
 for(uint64_t scanned=0;entries_left;){
  if(vqeaf_block_read(fs->dev_id,lba,1,sec))return -2;
  for(uint32_t off=0;off+32<=fs->bytes_per_sector&&entries_left;off+=32,entries_left--,scanned++){
   const uint8_t *e=sec+off;
   if(e[0]==0x00)return -3; /* end of directory */
   if(e[0]==0xE5||e[11]==0x0F)continue; /* deleted or LFN fragment: 8.3 only in v0.3 */
   if(!memcmp(e,name83,11)){
    uint32_t cl=rd16(e+26);uint32_t sz=rd32(e+28);
    if(e[11]&0x10){*first=cl;*size=0;*is_dir=1;}
    else{if(!cl)return -4;*first=cl;*size=sz;*is_dir=0;}
    return 0;
   }
  }
  if(dir_cluster==0){lba++;continue;}
  if(++sector_in_cluster<fs->sectors_per_cluster){lba++;continue;}
  sector_in_cluster=0;
  uint32_t next=0;
  if(fat_entry(fs,cluster,&next))return -5;
  if(next<0xFFF8&&next>=2){cluster=next;lba=cluster_lba(fs,cluster);entries_left=(uint64_t)fs->sectors_per_cluster*fs->bytes_per_sector/32u;}
  else return -3;
 }
 return -3;
}
/* ---- write support (v0.3.2): root-level files, no chain growth ---- */
#define FAT16_EOC 0xFFFFu
static int fat_set_entry(const VqeafFat16 *fs,uint32_t cluster,uint32_t value){
 if(cluster<2||cluster>=fs->cluster_count+2)return -1;
 uint64_t off=(uint64_t)cluster*2u;
 uint64_t lba=fs->fat_lba+off/fs->bytes_per_sector;
 uint8_t sec[VQEAF_SECTOR_MAX];
 if(vqeaf_block_read(fs->dev_id,lba,1,sec))return -2;
 uint32_t in_off=(uint32_t)(off%fs->bytes_per_sector);
 sec[in_off]=(uint8_t)value;sec[in_off+1]=(uint8_t)(value>>8);
 for(uint32_t c=0;c<fs->fat_count;c++) /* flush every FAT copy */
  if(vqeaf_block_write(fs->dev_id,fs->fat_lba+(uint64_t)c*fs->fat_sectors+off/fs->bytes_per_sector-fs->fat_lba,1,sec))return -3;
 return 0;
}
static int fat_alloc_cluster(const VqeafFat16 *fs,uint32_t *out){
 uint8_t sec[VQEAF_SECTOR_MAX];
 for(uint32_t cluster=2;cluster<fs->cluster_count+2;cluster++){
  uint64_t off=(uint64_t)cluster*2u;
  uint64_t lba=fs->fat_lba+off/fs->bytes_per_sector;
  uint32_t in_off=(uint32_t)(off%fs->bytes_per_sector);
  if(vqeaf_block_read(fs->dev_id,lba,1,sec))return -1;
  if(rd16(sec+in_off)==0){*out=cluster;return 0;}
 }
 return -2; /* volume full */
}
static int64_t chain_write(const VqeafFat16 *fs,uint32_t first,const void *buf,uint32_t len){
 if(!first)return -1;
 const uint8_t *src=buf;
 uint64_t done=0;uint32_t cluster=first;
 uint8_t sec[VQEAF_SECTOR_MAX];
 while(done<len&&cluster>=2&&cluster<0xFFF8&&cluster<=fs->cluster_count+1){
  uint64_t clba=cluster_lba(fs,cluster);
  for(uint32_t c=0;c<fs->sectors_per_cluster&&done<len;c++){
   uint64_t take=len-done<(uint64_t)fs->bytes_per_sector?len-done:(uint64_t)fs->bytes_per_sector;
   if(take<fs->bytes_per_sector){ /* partial sector: read-modify-write */
    if(vqeaf_block_read(fs->dev_id,clba+c,1,sec))return -2;
   }
   memcpy(sec,src+done,(size_t)take);
   if(vqeaf_block_write(fs->dev_id,clba+c,1,sec))return -3;
   done+=take;
  }
  uint32_t next=0;
  if(fat_entry(fs,cluster,&next))return -4;
  cluster=next;
 }
 return (int64_t)done;
}
typedef struct { uint64_t lba; uint32_t in_off; } DirSpot;
/* Scan the fixed-size root directory: returns 0 with spot for a matching
 * entry, 1 with spot for a free slot (0x00 end-of-dir or 0xE5 deleted), -2 full. */
static int dir_scan(const VqeafFat16 *fs,const uint8_t name83[11],DirSpot *spot){
 uint8_t sec[VQEAF_SECTOR_MAX];
 uint32_t entries=fs->root_entries;
 uint64_t lba=fs->root_lba;
 int free_found=0;
 while(entries){
  if(vqeaf_block_read(fs->dev_id,lba,1,sec))return -1;
  for(uint32_t k=0;k<fs->bytes_per_sector&&entries;k+=32,entries--){
   const uint8_t *e=sec+k;
   if(e[0]==0x00){ /* end-of-directory marker: usable slot */
    if(!free_found){spot->lba=lba;spot->in_off=k;free_found=1;}
    return 1;
   }
   if(e[0]==0xE5&&!free_found){spot->lba=lba;spot->in_off=k;free_found=1;continue;}
   if(!memcmp(e,name83,11)&&e[11]!=0x0F){spot->lba=lba;spot->in_off=k;return 0;}
  }
  lba++;
 }
 return free_found?1:-2;
}
int vqeaf_fat16_write_file(VqeafFat16Mount *mnt,const char *name,const void *data,uint32_t len){
 if(!mnt||!name||(!data&&len))return -1;
 uint8_t name83[11];int ok=0;
 name_to_83(name,name83,&ok);
 if(!ok)return -2;
 VqeafFat16 *fs=&mnt->fs;
 uint64_t cluster_bytes=(uint64_t)fs->bytes_per_sector*fs->sectors_per_cluster;
 uint8_t sec[VQEAF_SECTOR_MAX];
 DirSpot spot;
 int scan=dir_scan(fs,name83,&spot);
 if(scan<0)return -3; /* directory full */
 if(scan==0){ /* overwrite existing file in place (shrink only) */
  if(vqeaf_block_read(fs->dev_id,spot.lba,1,sec))return -3;
  uint8_t *e=sec+spot.in_off;
  uint32_t old_size=rd32(e+28),first=rd16(e+26);
  if(len>old_size)return -4; /* growth requires chain extension, not implemented */
  if(len){int64_t w=chain_write(fs,first,data,len);if(w<0||(uint32_t)w!=len)return -5;}
  wr32(e+28,len);
  if(vqeaf_block_write(fs->dev_id,spot.lba,1,sec))return -3;
  return (int)len;
 }
 /* create a new root-level file */
 uint32_t needed=len?(uint32_t)((len+cluster_bytes-1)/cluster_bytes):1;
 uint32_t first=0,prev=0;
 for(uint32_t i=0;i<needed;i++){
  uint32_t c=0;
  if(fat_alloc_cluster(fs,&c))return -6; /* volume full */
  if(fat_set_entry(fs,c,FAT16_EOC))return -3;
  if(prev&&fat_set_entry(fs,prev,c))return -3;
  if(!prev)first=c;
  prev=c;
 }
 if(len){int64_t w=chain_write(fs,first,data,len);if(w<0||(uint32_t)w!=len)return -5;}
 if(vqeaf_block_read(fs->dev_id,spot.lba,1,sec))return -3;
 uint8_t *e=sec+spot.in_off;
 memset(e,0,32);memset(e,' ',11);
 memcpy(e,name83,11);e[11]=0x20;
 wr16(e+26,(uint16_t)first);wr32(e+28,len);
 if(vqeaf_block_write(fs->dev_id,spot.lba,1,sec))return -3;
 return (int)len;
}

/* ---- subdirectory creation (v0.3.3) ---- */

/* Write a fresh directory cluster: . entry (self) and .. entry (parent). */
static int init_dir_cluster(const VqeafFat16 *fs,uint32_t self_cluster,uint32_t parent_cluster){
 uint8_t sec[VQEAF_SECTOR_MAX];
 memset(sec,0,fs->bytes_per_sector);
 /* . entry */
 uint8_t *e=sec;
 memcpy(e,".          ",11);
 e[11]=0x10; /* directory attribute */
 wr16(e+26,(uint16_t)self_cluster);
 wr32(e+28,0);
 /* .. entry */
 e=sec+32;
 memcpy(e,"..         ",11);
 e[11]=0x10;
 wr16(e+26,(uint16_t)parent_cluster);
 wr32(e+28,0);
 if(vqeaf_block_write(fs->dev_id,cluster_lba(fs,self_cluster),1,sec))return -1;
 return 0;
}

/* Add a directory entry for child_name/child_cluster into parent directory.
 * parent_cluster==0 means root directory. */
static int add_dir_entry(VqeafFat16Mount *mnt,uint32_t parent_cluster,
                         const uint8_t name83[11],uint32_t child_cluster){
 VqeafFat16 *fs=&mnt->fs;
 uint8_t sec[VQEAF_SECTOR_MAX];
 DirSpot spot;
 uint64_t lba;
 uint32_t entries;
 if(parent_cluster==0){lba=fs->root_lba;entries=fs->root_entries;}
 else{lba=cluster_lba(fs,parent_cluster);entries=(uint32_t)(fs->sectors_per_cluster*fs->bytes_per_sector/32u);}
 int free_found=0;
 uint64_t free_lba=0;uint32_t free_off=0;
 while(entries){
  if(vqeaf_block_read(fs->dev_id,lba,1,sec))return -1;
  for(uint32_t k=0;k<fs->bytes_per_sector&&entries;k+=32,entries--){
   const uint8_t *e=sec+k;
   if(e[0]==0x00){if(!free_found){free_lba=lba;free_off=k;free_found=1;}return 0;}
   if(e[0]==0xE5&&!free_found){free_lba=lba;free_off=k;free_found=1;continue;}
   if(!memcmp(e,name83,11)&&e[11]!=0x0F)return -2; /* already exists */
  }
  if(parent_cluster==0){lba++;continue;}
  /* subdirectory cluster chain - not needed for v0.3.3 root-only mkdir */
  break;
 }
 if(!free_found)return -3; /* directory full */
 if(vqeaf_block_read(fs->dev_id,free_lba,1,sec))return -1;
 uint8_t *e=sec+free_off;
 memset(e,0,32);memset(e,' ',11);
 memcpy(e,name83,11);e[11]=0x10; /* directory attribute */
 wr16(e+26,(uint16_t)child_cluster);
 wr32(e+28,0);
 if(vqeaf_block_write(fs->dev_id,free_lba,1,sec))return -1;
 return 0;
}

/* Create a single directory component in a parent directory.
 * parent_cluster==0 means root. Returns the new cluster or negative error. */
static int mkdir_one(VqeafFat16Mount *mnt,uint32_t parent_cluster,const char *name){
 VqeafFat16 *fs=&mnt->fs;
 uint8_t name83[11];int ok=0;
 name_to_83(name,name83,&ok);
 if(!ok)return -1;
 /* Reject if already exists */
 VqeafVnode *existing=0;
 if(vqeaf_fat16_lookup(mnt,&mnt->root_vnode,name,&existing)==0)return -2;
 uint32_t new_cluster=0;
 if(fat_alloc_cluster(fs,&new_cluster))return -3; /* volume full */
 if(fat_set_entry(fs,new_cluster,FAT16_EOC))return -4;
 if(init_dir_cluster(fs,new_cluster,parent_cluster))return -5;
 if(add_dir_entry(mnt,parent_cluster,name83,new_cluster))return -6;
 return (int)new_cluster;
}

/* Create a (possibly nested) directory path under the mount root.
 * Intermediate directories are created as needed. */
int vqeaf_fat16_mkdir(VqeafFat16Mount *mnt,const char *path){
 if(!mnt||!path)return -1;
 /* Skip leading slashes. */
 while(*path=='/')path++;
 if(!*path)return 0; /* "/" is root, already exists */

 /* Make a mutable copy for tokenising. */
 static char scratch[256];
 int n=0;
 const char *src=path;
 while(*src&&n<(int)sizeof(scratch)-1){scratch[n++]=*src++;}scratch[n]='\0';

 uint32_t cur_cluster=0; /* 0 = root directory */
 char built[256];int built_len=0;
 char *p=scratch;
 while(*p){
  while(*p=='/')p++;
  if(!*p)break;
  char *end=p;
  while(*end&&*end!='/')end++;
  char saved=*end;
  *end='\0';

  /* Append component to built path. */
  int comp_len=0;while(p[comp_len])comp_len++;
  if(built_len>0)built[built_len++]='/';
  for(int i=0;i<comp_len;i++)built[built_len++]=p[i];
  built[built_len]='\0';

  /* Check if this path already exists. */
  VqeafVnode *node=0;
  int r=vqeaf_fat16_walk(mnt,built,&node);
  if(r==0){
   FatNode *fn=(FatNode*)node->private_data;
   if(fn&&fn->is_dir){cur_cluster=fn->first_cluster;*end=saved;p=end;continue;}
   *end=saved;return -4; /* exists but not a directory */
  }
  /* Component missing: create it in the current directory. */
  int new_cluster=mkdir_one(mnt,cur_cluster,p);
  if(new_cluster<0){*end=saved;return new_cluster;}
  cur_cluster=(uint32_t)new_cluster;
  *end=saved;
  p=end;
 }
 return 0;
}

static int64_t chain_read(const VqeafFat16 *fs,uint32_t first,uint32_t limit,uint64_t offset,void *buf,uint64_t len){
 if(!fs||!buf)return -1;
 if(!first)return -2;
 uint64_t cluster_bytes=(uint64_t)fs->bytes_per_sector*fs->sectors_per_cluster;
 uint64_t total=limit?(uint64_t)limit:cluster_bytes*512u; /* unbounded streams capped defensively */
 if(offset>=total)return 0;
 if(len>total-offset)len=total-offset;
 uint8_t csec[VQEAF_SECTOR_MAX];
 uint64_t pos=0,done=0;
 uint32_t cluster=first;
 while(len&&cluster>=2&&cluster<0xFFF8&&cluster<=fs->cluster_count+1){
  if(cluster==0xFFF7)return -4; /* bad cluster */
  uint64_t clba=cluster_lba(fs,cluster);
  for(uint32_t c=0;c<fs->sectors_per_cluster&&done<len;c++){
   uint64_t sec_start=pos+(uint64_t)c*fs->bytes_per_sector;
   uint64_t sec_end=sec_start+fs->bytes_per_sector;
   if(sec_end<=offset||sec_start>=offset+len)continue;
   if(vqeaf_block_read(fs->dev_id,clba+c,1,csec))return -5;
   uint64_t from=offset>sec_start?offset-sec_start:0;
   uint64_t take=sec_end-sec_start-from;
   if(take>len-done)take=len-done;
   memcpy((uint8_t*)buf+done,csec+from,(size_t)take);
   done+=take;
  }
  pos+=cluster_bytes;
  uint32_t next=0;
  if(fat_entry(fs,cluster,&next))return -6;
  cluster=next;
 }
 return (int64_t)done;
}
int vqeaf_fat16_lookup(VqeafFat16Mount *mnt,VqeafVnode *dir,const char *name,VqeafVnode **out){
 if(!mnt||!dir||!out||!name)return -1;
 FatNode *d=(FatNode*)dir->private_data;
 if(!d||!d->is_dir)return -2;
 uint8_t name83[11];int ok=0;
 name_to_83(name,name83,&ok);
 if(!ok)return -3;
 FatNode *n=vqeaf_fat_alloc_node(mnt);
 if(!n)return -4;
 uint32_t first=0,size=0;int is_dir=0;
 int r=dir_lookup(&mnt->fs,d->first_cluster,name83,&first,&size,&is_dir);
 if(r){mnt->node_count--;return r;}
 n->mnt=mnt;n->first_cluster=first;n->size=size;n->is_dir=(uint8_t)is_dir;
 n->vnode.ops=&vqeaf_fat16_vfs_ops;n->vnode.private_data=n;
 n->vnode.size=is_dir?0:size;n->vnode.type=is_dir?1u:0u;n->vnode.flags=0;
 *out=&n->vnode;return 0;
}
int vqeaf_fat16_walk(VqeafFat16Mount *mnt,const char *path,VqeafVnode **out){
 if(!mnt||!path||!out||vqeaf_vfs_validate_path(path))return -1;
 VqeafVnode *cur=&mnt->root_vnode;
 char comp[VQEAF_FAT_NAME_MAX];
 uint64_t i=0;
 while(path[i]){
  while(path[i]=='/')i++;
  if(!path[i])break;
  uint64_t j=i;
  while(path[j]&&path[j]!='/')j++;
  if(j-i>=sizeof(comp))return -2;
  memcpy(comp,path+i,(size_t)(j-i));comp[j-i]=0;
  VqeafVnode *next=0;
  int r=vqeaf_fat16_lookup(mnt,cur,comp,&next);
  if(r)return r;
  cur=next;i=j;
 }
 *out=cur;return 0;
}
static int64_t fat_read_vnode(VqeafVnode *node,uint64_t offset,void *buf,uint64_t size){
 if(!node||!buf)return -1;
 FatNode *n=(FatNode*)node->private_data;
 if(!n)return -1;
 if(n->is_dir)return -2; /* directory enumeration is separate; vnodes expose file bytes */
 if(offset>=n->size)return 0;
 return chain_read(&n->mnt->fs,n->first_cluster,n->size,offset,buf,size>n->size-offset?n->size-offset:size);
}
const VqeafVfsOps vqeaf_fat16_vfs_ops={0,fat_read_vnode,0,0};
