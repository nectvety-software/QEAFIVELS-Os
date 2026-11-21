#include <string.h>
#include "include/vqeaf/partition.h"
#define MBR_SIG_OFFSET 510u
#define MBR_ENTRY_OFFSET 446u
#define MBR_ENTRY_SIZE 16u
static uint16_t rd16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t rd32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t rd64(const uint8_t *p){return (uint64_t)rd32(p)|((uint64_t)rd32(p+4)<<32);}
int vqeaf_gpt_present(uint32_t dev_id){
 uint8_t sec[VQEAF_SECTOR_MAX];
 struct VqeafBlockDevice *d=0;
 if(vqeaf_block_get(dev_id,&d)||!d)return -1;
 if(vqeaf_block_read(dev_id,0,1,sec))return -1;
 if(rd16(sec+MBR_SIG_OFFSET)!=0xAA55)return 0;
 /* Protective MBR: exactly one entry of type 0xEE covering the disk. */
 const uint8_t *e=sec+MBR_ENTRY_OFFSET;
 if(e[4]==VQEAF_PART_TYPE_GPT)return 1;
 return 0;
}
int vqeaf_mbr_parse(uint32_t dev_id,VqeafPartition *out,uint32_t max){
 uint8_t sec[VQEAF_SECTOR_MAX];
 struct VqeafBlockDevice *d=0;
 if(!out||!max)return -1;
 if(vqeaf_block_get(dev_id,&d)||!d)return -1;
 if(vqeaf_block_read(dev_id,0,1,sec))return -2;
 if(rd16(sec+MBR_SIG_OFFSET)!=0xAA55)return -3;
 uint32_t n=0;
 for(unsigned i=0;i<4&&n<max;i++){
  const uint8_t *e=sec+MBR_ENTRY_OFFSET+i*MBR_ENTRY_SIZE;
  uint8_t type=e[4];
  uint32_t start=rd32(e+8),count=rd32(e+12);
  if(!type||!count)continue; /* empty slot */
  if(start>d->sector_count||d->sector_count-start<(uint64_t)count)return -4; /* lies past disk end */
  /* Reject overlaps between parsed entries. */
  for(uint32_t j=0;j<n;j++){
   uint64_t s0=out[j].start_lba,e0=out[j].start_lba+out[j].sector_count;
   uint64_t s1=start,e1=(uint64_t)start+count;
   if(s1<e0&&s0<e1)return -5;
  }
  out[n].start_lba=start;out[n].sector_count=count;out[n].type=type;out[n].mbr_index=(uint8_t)i;n++;
 }
 return (int)n;
}
int vqeaf_gpt_parse(uint32_t dev_id,VqeafPartition *out,uint32_t max){
 uint8_t hdr[VQEAF_SECTOR_MAX],ent[VQEAF_SECTOR_MAX];
 struct VqeafBlockDevice *d=0;
 if(!out||!max)return -1;
 if(vqeaf_block_get(dev_id,&d)||!d)return -1;
 if(d->sector_size<512)return -2;
 if(vqeaf_block_read(dev_id,1,1,hdr))return -2;
 if(memcmp(hdr,"EFI PART",8)!=0)return -3; /* not a GPT header */
 uint32_t hdr_size=rd32(hdr+12);
 if(hdr_size<92||hdr_size>d->sector_size)return -4;
 uint64_t entries_lba=rd64(hdr+72);
 uint32_t num_entries=rd32(hdr+80),entry_size=rd32(hdr+84);
 if(!num_entries||num_entries>128||entry_size<128||entry_size>1024)return -5;
 if(entries_lba<2||entries_lba>d->sector_count)return -6;
 uint64_t entries_per_sector=d->sector_size/entry_size;
 uint32_t n=0;
 for(uint32_t i=0;i<num_entries&&n<max;i++){
  uint32_t within=(uint32_t)(i%entries_per_sector);
  uint32_t idx=(uint32_t)(i/entries_per_sector);
  if(within==0&&vqeaf_block_read(dev_id,entries_lba+idx,1,ent))return -7;
  const uint8_t *e=ent+(uint64_t)within*entry_size;
  static const uint8_t zero_guid[8]={0};
  if(!memcmp(e,zero_guid,8))continue; /* unused entry */
  uint64_t start=rd64(e+32),end=rd64(e+40);
  if(end<start)return -8;
  uint64_t count=end-start+1;
  if(start>d->sector_count||d->sector_count-start<count)return -8;
  for(uint32_t j=0;j<n;j++){
   uint64_t s0=out[j].start_lba,e0=s0+out[j].sector_count;
   if(start<e0&&s0<start+count)return -9;
  }
  out[n].start_lba=start;out[n].sector_count=count;out[n].type=0xEE;out[n].mbr_index=0xFF;n++;
 }
 return (int)n;
}
