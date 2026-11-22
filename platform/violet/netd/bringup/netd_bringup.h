/* SPDX-License-Identifier: MIT */
#ifndef QEAF_NETD_BRINGUP_H
#define QEAF_NETD_BRINGUP_H
#include <stddef.h>

/* Runs the 5-step Wi-Fi ladder read-only. Writes net: evidence lines.
 * Returns number of verified steps, or -1 when blocked at step N.
 * Never publishes a QMI service or writes firmware/partitions. */
int netd_bringup_run(char *out, size_t cap);
const char *netd_bringup_last_reason(void);

#endif
