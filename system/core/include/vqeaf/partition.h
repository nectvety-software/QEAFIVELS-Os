#ifndef VQEAF_PARTITION_H
#define VQEAF_PARTITION_H
#include <stdint.h>
#include "block.h"
#define VQEAF_PART_MAX 16u
#define VQEAF_PART_TYPE_GPT 0xEEu
typedef struct { uint64_t start_lba,sector_count; uint8_t type,mbr_index; } VqeafPartition;
/* MBR: validates signature and entries; returns number of usable partitions or <0. */
int vqeaf_mbr_parse(uint32_t dev_id,VqeafPartition *out,uint32_t max);
/* Returns 1 if the disk carries a protective MBR pointing at a GPT, else 0. */
int vqeaf_gpt_present(uint32_t dev_id);
/* GPT v0.3.2: reads the header at LBA 1 and the partition entry array,
 * validating signature, bounds and overlaps. CRC32 verification is pending. */
int vqeaf_gpt_parse(uint32_t dev_id,VqeafPartition *out,uint32_t max);
#endif
