#include <stdint.h>
#include "../../include/vqeaf/rtc.h"
#include "../../include/vqeaf/serial.h"

#define CMOS_ADDR 0x70u
#define CMOS_DATA 0x71u
#define CMOS_STATUS_A 0x0Au
#define CMOS_STATUS_B 0x0Bu

static uint8_t cmos_read(uint8_t reg) {
    vqeaf_out8(CMOS_ADDR, reg);
    return vqeaf_in8(CMOS_DATA);
}
static int cmos_busy(void) { return (cmos_read(CMOS_STATUS_A) & 0x80u) != 0; }
static uint32_t bcd_to_bin(uint32_t v) {
    return (v & 0x0Fu) + ((v >> 4) * 10u);
}
static void read_raw(VqeafRtcTime *t) {
    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour = cmos_read(0x04);
    t->day = cmos_read(0x07);
    t->month = cmos_read(0x08);
    t->year = cmos_read(0x09);
}

int vqeaf_rtc_read(VqeafRtcTime *out) {
    if (!out) return -1;
    out->valid = 0;
    for (uint32_t i = 0; i < 1000000u; i++)
        if (!cmos_busy()) break;
    VqeafRtcTime a, b;
    read_raw(&a);
    read_raw(&b);
    if (a.second != b.second || a.minute != b.minute || a.hour != b.hour ||
        a.day != b.day || a.month != b.month || a.year != b.year) return -1;
    uint8_t status_b = cmos_read(CMOS_STATUS_B);
    int binary = (status_b & 0x04u) != 0;
    int hour12 = (status_b & 0x02u) == 0;
    uint32_t pm = hour12 ? (a.hour & 0x80u) : 0u;
    if (hour12) a.hour &= 0x7Fu;
    if (!binary) {
        a.second = bcd_to_bin(a.second);
        a.minute = bcd_to_bin(a.minute);
        a.hour = bcd_to_bin(a.hour);
        a.day = bcd_to_bin(a.day);
        a.month = bcd_to_bin(a.month);
        a.year = bcd_to_bin(a.year);
    }
    if (hour12) {
        if (pm && a.hour < 12u) a.hour += 12u;
        else if (!pm && a.hour == 12u) a.hour = 0u;
    }
    uint32_t year = a.year;
    if (year < 100u) {
        uint8_t century = cmos_read(0x32);
        if (century != 0x00u && century != 0xFFu) {
            if (!binary) century = (uint8_t)bcd_to_bin(century);
            year = (uint32_t)century * 100u + a.year;
        } else {
            year = 2000u + a.year;
        }
    }
    if (year < 2000u || year > 2100u || a.month < 1u || a.month > 12u ||
        a.day < 1u || a.day > 31u || a.hour > 23u || a.minute > 59u ||
        a.second > 59u) return -1;
    out->year = year;
    out->month = a.month;
    out->day = a.day;
    out->hour = a.hour;
    out->minute = a.minute;
    out->second = a.second;
    out->valid = 1;
    return 0;
}
