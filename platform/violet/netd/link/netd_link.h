/* SPDX-License-Identifier: MIT */
#ifndef QEAF_NETD_LINK_H
#define QEAF_NETD_LINK_H
#include <stdint.h>
#include <stddef.h>

typedef void (*netd_link_cb)(const char *name, int up, void *ctx);

/* Parse one or more NETLINK_ROUTE RTM_GETLINK dump messages. Appends unique
 * "name" entries to out (comma separated). Returns count or -errno-style <0. */
int  netd_link_parse(const uint8_t *buf, size_t len, char *out, size_t cap);
/* Snapshot of current interfaces, names only. */
int  netd_link_list(char *out, size_t cap);
/* 1 when the exact interface exists (checked through sysfs, no guessing). */
int  netd_link_exists(const char *name);
/* Bring an interface up/down through SIOCSIFFLAGS. */
int  netd_link_set_up(const char *name, int up);
/* Subscribe to RTMGRP_LINK and invoke cb on NEWLINK/DELLINK. One-shot poll
 * with timeout_ms; returns 0 on timeout or -errno. */
int  netd_link_hotplug(netd_link_cb cb, void *ctx, int timeout_ms);

#endif
