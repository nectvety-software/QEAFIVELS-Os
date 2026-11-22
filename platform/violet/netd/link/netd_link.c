/* SPDX-License-Identifier: MIT */
/* link: raw rtnetlink enumeration + hotplug, ioctl for flags. */
#define _GNU_SOURCE
#include "netd_link.h"

#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define NL_BUF 8192
#define IFNAME_MAX 64

/* Bounded parser: walks nlmsghdr + rtattr and extracts IFLA_IFNAME only.
 * Works on live netlink buffers and on recorded FIXTURE buffers. */
int netd_link_parse(const uint8_t *buf, size_t len, char *out, size_t cap)
{
    size_t off = 0;
    int count = 0;
    if(buf == NULL || out == NULL || cap == 0) return -1;
    out[0] = '\0';
    while(off + sizeof(struct nlmsghdr) <= len) {
        const struct nlmsghdr *nh = (const struct nlmsghdr *)(buf + off);
        size_t mlen;
        if(nh->nlmsg_len < sizeof(struct nlmsghdr)) return -2;
        mlen = nh->nlmsg_len;
        if(mlen > len - off) return -3;
        if(nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_GETLINK) {
            const struct ifinfomsg *ifm =
                (const struct ifinfomsg *)((const uint8_t *)nh +
                                            NLMSG_ALIGN(sizeof(*nh)));
            size_t aoff = NLMSG_ALIGN(sizeof(*nh)) + NLMSG_ALIGN(sizeof(*ifm));
            char name[IFNAME_MAX];
            size_t used = 0;
            if(aoff > mlen) return -4;
            while(aoff + sizeof(struct rtattr) <= mlen) {
                const struct rtattr *ra =
                    (const struct rtattr *)((const uint8_t *)nh + aoff);
                size_t alen = ra->rta_len;
                if(alen < sizeof(struct rtattr) || alen > mlen - aoff) return -5;
                if(ra->rta_type == IFLA_IFNAME) {
                    size_t n = alen - RTA_LENGTH(0);
                    if(n >= sizeof(name)) n = sizeof(name) - 1;
                    memcpy(name, RTA_DATA(ra), n);
                    name[n] = '\0';
                    used = 1;
                }
                aoff += NLMSG_ALIGN(alen);
            }
            if(used && name[0] != '\0') {
                size_t cur = strlen(out);
                if(cur + used + 2u < cap) {
                    if(cur != 0) out[cur++] = ',';
                    memcpy(out + cur, name, used);
                    out[cur + used] = '\0';
                    count++;
                }
            }
        }
        if(nh->nlmsg_type == NLMSG_DONE) break;
        if(mlen < NLMSG_HDRLEN) return -6;
        off += NLMSG_ALIGN(mlen);
    }
    return count;
}

int netd_link_list(char *out, size_t cap)
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    uint8_t buf[NL_BUF];
    struct {
        struct nlmsghdr nh;
        struct ifinfomsg ifm;
    } req;
    ssize_t n;
    if(fd < 0) return -errno;
    memset(&req, 0, sizeof(req));
    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(req.ifm));
    req.nh.nlmsg_type = RTM_GETLINK;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nh.nlmsg_seq = 1;
    req.ifm.ifi_family = AF_UNSPEC;
    if(send(fd, &req, req.nh.nlmsg_len, 0) < 0) {
        int e = -errno; close(fd); return e;
    }
    n = recv(fd, buf, sizeof(buf), 0);
    close(fd);
    if(n <= 0) return n < 0 ? -errno : -1;
    return netd_link_parse(buf, (size_t)n, out, cap);
}

int netd_link_exists(const char *name)
{
    char path[128];
    if(name == NULL || name[0] == '\0') return 0;
    snprintf(path, sizeof(path), "/sys/class/net/%s", name);
    return access(path, F_OK) == 0 ? 1 : 0;
}

int netd_link_set_up(const char *name, int up)
{
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    short flags;
    if(fd < 0) return -errno;
    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, IFNAMSIZ, "%s", name);
    if(ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        int e = -errno; close(fd); return e;
    }
    flags = ifr.ifr_flags;
    if(up) flags |= IFF_UP; else flags &= ~IFF_UP;
    ifr.ifr_flags = flags;
    if(ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
        int e = -errno; close(fd); return e;
    }
    close(fd);
    return 0;
}

int netd_link_hotplug(netd_link_cb cb, void *ctx, int timeout_ms)
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    struct sockaddr_nl sa;
    uint8_t buf[NL_BUF];
    if(fd < 0) return -errno;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = RTMGRP_LINK;
    if(bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        int e = -errno; close(fd); return e;
    }
    /* Caller drives epoll; this one-shot read returns the pending event. */
    (void)timeout_ms;
    ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    if(n <= 0) { close(fd); return n < 0 ? -errno : 0; }
    size_t off = 0;
    while(off + sizeof(struct nlmsghdr) <= (size_t)n) {
        const struct nlmsghdr *nh = (const struct nlmsghdr *)(buf + off);
        if(nh->nlmsg_len < sizeof(*nh) || nh->nlmsg_len > (size_t)n - off) break;
        if(nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK) {
            char one[256];
            if(netd_link_parse((const uint8_t *)nh, nh->nlmsg_len,
                               one, sizeof(one)) > 0)
                cb(one, nh->nlmsg_type == RTM_NEWLINK, ctx);
        }
        off += NLMSG_ALIGN(nh->nlmsg_len);
    }
    close(fd);
    return 0;
}
