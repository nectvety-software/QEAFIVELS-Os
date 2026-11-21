#ifndef VQEAF_BLOCK_H
#define VQEAF_BLOCK_H
#include <stdint.h>
/* Block device abstraction v1: fixed sector size, bounded LBA range, checked
 * bounds on every request. Devices are registered into a bounded registry. */
#define VQEAF_BLOCK_MAX 8u
#define VQEAF_SECTOR_MAX 4096u
struct VqeafBlockDevice {
  const char *name;
  uint32_t sector_size;
  uint64_t sector_count;
  int (*read_sectors)(struct VqeafBlockDevice *dev,uint64_t lba,uint32_t count,void *out);
  int (*write_sectors)(struct VqeafBlockDevice *dev,uint64_t lba,uint32_t count,const void *in);
  void *ctx;
};
int vqeaf_block_register(struct VqeafBlockDevice *dev); /* returns id >= 0 */
int vqeaf_block_count(void);
int vqeaf_block_read(uint32_t id,uint64_t lba,uint32_t count,void *out);
int vqeaf_block_write(uint32_t id,uint64_t lba,uint32_t count,const void *in);
int vqeaf_block_get(uint32_t id,struct VqeafBlockDevice **out);
void vqeaf_block_reset(void);
#endif
