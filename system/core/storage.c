#include <string.h>
#include "include/vqeaf/storage.h"
#include "include/vqeaf/fs_fat16.h"
typedef struct {char path[96];VqeafVnode *root;uint32_t flags;int fat_slot;} Mount;
static Mount mounts[VQEAF_MOUNT_MAX];static uint32_t count;
/* Each FAT16 mount needs its own instance: sharing one would let a second
 * mount overwrite the first volume's node pool. */
static VqeafFat16Mount fat_pool[VQEAF_MOUNT_MAX];static uint8_t fat_used[VQEAF_MOUNT_MAX];
static int fat_acquire(void){for(uint32_t i=0;i<VQEAF_MOUNT_MAX;i++)if(!fat_used[i]){fat_used[i]=1;return (int)i;}return -1;}
static void fat_release(int slot){if(slot>=0&&slot<(int)VQEAF_MOUNT_MAX)fat_used[slot]=0;}
void vqeaf_storage_reset(void){memset(mounts,0,sizeof(mounts));memset(fat_used,0,sizeof(fat_used));count=0;}
static int mount_add(const char*p,VqeafVnode*r,uint32_t f,int fat_slot){
 if(!p||!r||vqeaf_vfs_validate_path(p)||strlen(p)>=sizeof(mounts[0].path))return -1;
 for(uint32_t i=0;i<count;i++)if(strcmp(mounts[i].path,p)==0)return -2;
 if(count==VQEAF_MOUNT_MAX)return -3;
 strcpy(mounts[count].path,p);mounts[count].root=r;mounts[count].flags=f;
 mounts[count].fat_slot=fat_slot;count++;return 0;
}
int vqeaf_storage_mount(const char*p,VqeafVnode*r,uint32_t f){return mount_add(p,r,f,-1);}
/* A new mount is refused if it duplicates an existing path or would hide a
 * deeper mount (mounting "/" over "/data", or "/data" over "/data/cache").
 * Mounting deeper paths below an existing mount is allowed: that is what the
 * longest-prefix resolve is for. */
static int mount_shadows(const char *path){
 size_t n=strlen(path);
 for(uint32_t i=0;i<count;i++){
  const char *mp=mounts[i].path;
  if(strcmp(mp,path)==0)return 1;
  if(n==1&&path[0]=='/')return 1;
  if(strncmp(mp,path,n)==0&&mp[n]=='/')return 1;
 }
 return 0;
}
/* Mounts a FAT16 volume (raw or partition base) at a mount path; refuses to
 * shadow an existing mount. Returns the storage mount result code. */
int vqeaf_storage_mount_fat16(uint32_t dev_id,uint64_t base_lba,const char *path,uint32_t flags){
 if(!path||vqeaf_vfs_validate_path(path)||strlen(path)>=sizeof(mounts[0].path))return -1;
 if(mount_shadows(path))return -5;
 int slot=fat_acquire();
 if(slot<0)return -4;
 int r=vqeaf_fat16_mount(dev_id,base_lba,&fat_pool[slot]);
 if(r){fat_release(slot);return r-100; /* FS-specific errors kept distinguishable */}
 r=mount_add(path,&fat_pool[slot].root_vnode,flags,slot);
 if(r){fat_release(slot);return r;}
 return 0;
}
int vqeaf_storage_unmount(const char*p){
 if(!p)return -1;
 for(uint32_t i=0;i<count;i++)if(strcmp(mounts[i].path,p)==0){
  fat_release(mounts[i].fat_slot);
  for(uint32_t j=i+1;j<count;j++)mounts[j-1]=mounts[j];
  count--;return 0;
 }
 return -2;
}
VqeafFat16Mount *vqeaf_storage_fat_mount(const char *path){
 if(!path)return 0;
 for(uint32_t i=0;i<count;i++){
  if(strcmp(mounts[i].path,path)==0){
   if(mounts[i].fat_slot<0)return 0;
   return &fat_pool[mounts[i].fat_slot];
  }
 }
 return 0;
}
static int resolve_index(const char*p,uint32_t*idx,const char**rel){
 int best=-1;size_t best_len=0;const char *best_rel=0;
 for(uint32_t i=0;i<count;i++){
  const char *mp=mounts[i].path;
  size_t n=strlen(mp);
  if(n==1&&mp[0]=='/'){ /* root mount matches every absolute path */
   if(best_len<1u){best=(int)i;best_len=1u;best_rel=p;}
   continue;
  }
  if(n>=best_len&&strncmp(p,mp,n)==0&&(p[n]=='/'||p[n]==0)){best=(int)i;best_len=n;best_rel=p+n;}
 }
 if(best<0)return -2;
 *idx=(uint32_t)best;*rel=best_rel;return 0;
}
int vqeaf_storage_resolve(const char*p,VqeafVnode**node,const char**rel){
 if(!p||!node||!rel||vqeaf_vfs_validate_path(p))return -1;
 uint32_t idx=0;
 int r=resolve_index(p,&idx,rel);
 if(r)return r;
 *node=mounts[idx].root;return 0;
}
int vqeaf_storage_walk(const char*p,VqeafVnode**node){
 if(!p||!node||vqeaf_vfs_validate_path(p))return -1;
 uint32_t idx=0;const char *rel=0;
 int r=resolve_index(p,&idx,&rel);
 if(r)return r;
 if(!rel[0]||strcmp(rel,"/")==0){*node=mounts[idx].root;return 0;}
 if(mounts[idx].fat_slot<0)return -3; /* opaque mount root: no lookup behind it */
 return vqeaf_fat16_walk(&fat_pool[mounts[idx].fat_slot],rel,node);
}
