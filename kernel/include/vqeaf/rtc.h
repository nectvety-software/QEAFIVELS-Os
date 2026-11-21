#ifndef VQEAF_RTC_H
#define VQEAF_RTC_H
#include <stdint.h>
/* CMOS real-time clock over ports 0x70/0x71. The time is whatever the firmware
 * battery-backed clock holds; this driver never applies a timezone. */
typedef struct {
    uint32_t year, month, day, hour, minute, second;
    int valid;
} VqeafRtcTime;
int vqeaf_rtc_read(VqeafRtcTime *out); /* 0 ok; -1 arg or unstable/invalid */
#endif
