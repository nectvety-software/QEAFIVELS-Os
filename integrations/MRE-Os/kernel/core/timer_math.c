#include "mre_timer.h"
uint64_t mre_timer_ticks_from_frequency(uint64_t frequency_hz, uint64_t microseconds)
{
    uint64_t whole, remainder;
    if (frequency_hz == 0u) return 0u;
    whole = frequency_hz / 1000000u;
    remainder = frequency_hz % 1000000u;
    if (microseconds != 0u && whole > UINT64_MAX / microseconds) return UINT64_MAX;
    whole *= microseconds;
    if (remainder != 0u) {
        uint64_t fraction;
        if (microseconds > UINT64_MAX / remainder) return UINT64_MAX;
        fraction = (remainder * microseconds) / 1000000u;
        if (whole > UINT64_MAX - fraction) return UINT64_MAX;
        whole += fraction;
    }
    return whole;
}
