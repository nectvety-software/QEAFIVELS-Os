#ifndef MRE_TIMER_H
#define MRE_TIMER_H
#include "mre_types.h"
typedef struct mre_timer_info { uint64_t frequency_hz; uint32_t irq; uint8_t available; } mre_timer_info_t;
uint64_t mre_timer_ticks_from_frequency(uint64_t frequency_hz, uint64_t microseconds);
mre_status_t mre_timer_init(uint32_t irq);
uint64_t mre_timer_frequency(void);
uint64_t mre_timer_counter(void);
uint64_t mre_timer_ticks_from_us(uint64_t microseconds);
uint64_t mre_timer_ticks_elapsed(void);
uint32_t mre_timer_irq_number(void);
mre_status_t mre_timer_set_periodic(uint64_t period_us);
void mre_timer_stop(void);
void mre_timer_irq(void);
#endif
