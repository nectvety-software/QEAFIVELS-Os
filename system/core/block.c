#include <string.h>
#include "include/vqeaf/block.h"
static struct VqeafBlockDevice *devices[VQEAF_BLOCK_MAX];
static uint32_t count;
void vqeaf_block_reset(void){memset(devices,0,sizeof(devices));count=0;}
int vqeaf_block_register(struct VqeafBlockDevice *dev){
 if(!dev||!dev->name||!dev->read_sectors)return -1;
 if(!dev->sector_size||dev->sector_size>VQEAF_SECTOR_MAX||(dev->sector_size&(dev->sector_size-1)))return -1;
 if(!dev->sector_count)return -1;
 if(count==VQEAF_BLOCK_MAX)return -2;
 for(uint32_t i=0;i<count;i++)if(strcmp(devices[i]->name,dev->name)==0)return -3;
 devices[count]=dev;return (int)count++;
}
int vqeaf_block_count(void){return (int)count;}
int vqeaf_block_get(uint32_t id,struct VqeafBlockDevice **out){
 if(id>=count||!out)return -1;*out=devices[id];return 0;
}
int vqeaf_block_read(uint32_t id,uint64_t lba,uint32_t sectors,void *out){
 if(id>=count||!out||!sectors)return -1;
 struct VqeafBlockDevice *d=devices[id];
 if(lba>d->sector_count||d->sector_count-lba<(uint64_t)sectors)return -2; /* checked: no wrap (lba<=count) */
 return d->read_sectors(d,lba,sectors,out);
}
int vqeaf_block_write(uint32_t id,uint64_t lba,uint32_t sectors,const void *in){
 if(id>=count||!in||!sectors)return -1;
 struct VqeafBlockDevice *d=devices[id];
 if(!d->write_sectors)return -3; /* read-only media is explicit, never silent */
 if(lba>d->sector_count||d->sector_count-lba<(uint64_t)sectors)return -2;
 return d->write_sectors(d,lba,sectors,in);
}
