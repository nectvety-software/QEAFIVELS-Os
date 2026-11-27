#include <errno.h>
#include <stdint.h>
#include "mre_mmu.h"
int vqeaf_violet_mmu_is_enabled(void) { return 0; }
int vqeaf_violet_irq_is_active(void) { return 0; }
int vqeaf_violet_irq_request(uint32_t irq, void (*handler)(uint32_t, void *), void *ctx)
{ (void)irq; (void)handler; (void)ctx; return -ENOSYS; }
int vqeaf_violet_irq_release(uint32_t irq) { (void)irq; return -ENOSYS; }
mre_status_t mre_mmu_map_range(const mre_mmu_region_t *region)
{ (void)region; return MRE_E_UNSUPPORTED; }
