/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_wifi.c - real scan/connect seam for the shell. */
#define _GNU_SOURCE

#include "vqeaf_wifi.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "vqeaf_state.h"

#ifdef __linux__

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/wireless.h>

#define SCAN_BUFFER 32768

static const char *wifi_ifname(void)
{
    if(vqeaf_state.wifi_ifname[0] != '\0')
        return vqeaf_state.wifi_ifname;
    return "wlan0";
}

static int open_ioctl(const char *ifname, int *fd)
{
    struct iwreq wrq;

    *fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(*fd < 0) return -errno;

    memset(&wrq, 0, sizeof(wrq));
    snprintf(wrq.ifr_name, IFNAMSIZ, "%s", ifname);
    if(ioctl(*fd, SIOCGIWNAME, &wrq) < 0) {
        int err = -errno;
        close(*fd);
        *fd = -1;
        return err == 0 ? -ENODEV : err;
    }
    return 0;
}

static void add_network(vqeaf_wifi_net_t *out, int max, int *count,
                        const char *ssid, const char *bssid,
                        int signal, int secure)
{
    if(ssid == NULL || ssid[0] == '\0') return;
    for(int i = 0; i < *count; i++) {
        if(strcmp(out[i].ssid, ssid) == 0) {
            if(signal > out[i].signal) {
                out[i].signal = signal;
                snprintf(out[i].bssid, sizeof(out[i].bssid), "%s", bssid);
            }
            out[i].secure |= secure;
            return;
        }
    }
    if(*count >= max) return;
    snprintf(out[*count].ssid, sizeof(out[*count].ssid), "%s", ssid);
    snprintf(out[*count].bssid, sizeof(out[*count].bssid), "%s", bssid);
    out[*count].signal = signal;
    out[*count].secure = secure;
    (*count)++;
}

int vqeaf_wifi_scan(vqeaf_wifi_net_t *out, int max)
{
    const char *ifname = wifi_ifname();
    struct iwreq wrq;
    char buffer[SCAN_BUFFER];
    int fd = -1;
    int rc;
    int ready = 0;

    if(out == NULL || max <= 0) return -EINVAL;
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    rc = open_ioctl(ifname, &fd);
    if(rc < 0) return rc;

    memset(&wrq, 0, sizeof(wrq));
    snprintf(wrq.ifr_name, IFNAMSIZ, "%s", ifname);
    wrq.u.data.pointer = NULL;
    wrq.u.data.length = 0;
    wrq.u.data.flags = 0;
    if(ioctl(fd, SIOCSIWSCAN, &wrq) < 0 && errno != EAGAIN) {
        rc = -errno;
        close(fd);
        return rc;
    }

    /* WEXT has no completion event here; poll the result buffer briefly. */
    for(int attempt = 0; attempt < 30; attempt++) {
        memset(&wrq, 0, sizeof(wrq));
        snprintf(wrq.ifr_name, IFNAMSIZ, "%s", ifname);
        memset(buffer, 0, sizeof(buffer));
        wrq.u.data.pointer = buffer;
        wrq.u.data.length = sizeof(buffer);
        wrq.u.data.flags = 0;
        if(ioctl(fd, SIOCGIWSCAN, &wrq) == 0) {
            ready = 1;
            break;
        }
        if(errno != EAGAIN) break;
        usleep(150000);
    }
    if(!ready) {
        close(fd);
        return -EAGAIN;
    }

    {
        int count = 0;
        char ssid[33] = "";
        char bssid[18] = "";
        int signal = 0;
        int secure = 0;
        char *cursor = buffer;
        char *end = buffer + wrq.u.data.length;

        while(cursor + sizeof(struct iw_event) <= end) {
            struct iw_event *event = (struct iw_event *)cursor;
            if(event->len < sizeof(struct iw_event) ||
               cursor + event->len > end)
                break;

            switch(event->cmd) {
            case SIOCGIWAP:
                add_network(out, max, &count, ssid, bssid, signal, secure);
                memset(ssid, 0, sizeof(ssid));
                signal = 0;
                secure = 0;
                snprintf(bssid, sizeof(bssid),
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         event->u.ap_addr.sa_data[0] & 0xff,
                         event->u.ap_addr.sa_data[1] & 0xff,
                         event->u.ap_addr.sa_data[2] & 0xff,
                         event->u.ap_addr.sa_data[3] & 0xff,
                         event->u.ap_addr.sa_data[4] & 0xff,
                         event->u.ap_addr.sa_data[5] & 0xff);
                break;
            case SIOCGIWESSID: {
                int length = event->u.essid.length;
                const char *name = (const char *)event->u.essid.pointer;
                if(name != NULL && length > 0) {
                    if(length > (int)sizeof(ssid) - 1)
                        length = (int)sizeof(ssid) - 1;
                    memcpy(ssid, name, (size_t)length);
                    ssid[length] = '\0';
                }
                break;
            }
            case SIOCGIWENCODE:
                secure = (event->u.encoding.flags & IW_ENCODE_DISABLED) ? 0 : 1;
                break;
            case IWEVQUAL:
                signal = event->u.qual.qual;
                if(signal > 100) signal = 100;
                if(signal < 0) signal = 0;
                break;
            default:
                break;
            }
            cursor += event->len;
        }
        add_network(out, max, &count, ssid, bssid, signal, secure);

        /* Strongest first; insertion sort over a 16-entry list. */
        for(int i = 1; i < count; i++) {
            vqeaf_wifi_net_t key = out[i];
            int j = i - 1;
            while(j >= 0 && out[j].signal < key.signal) {
                out[j + 1] = out[j];
                j--;
            }
            out[j + 1] = key;
        }
        close(fd);
        return count;
    }
}

int vqeaf_wifi_connect(const char *ssid, const char *password)
{
    const char *ifname = wifi_ifname();
    struct iwreq wrq;
    int fd = -1;
    int rc;

    if(ssid == NULL || ssid[0] == '\0') return -EINVAL;
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    if(password != NULL && password[0] != '\0')
        return -EOPNOTSUPP;

    rc = open_ioctl(ifname, &fd);
    if(rc < 0) return rc;

    memset(&wrq, 0, sizeof(wrq));
    snprintf(wrq.ifr_name, IFNAMSIZ, "%s", ifname);
    wrq.u.essid.pointer = (void *)ssid;
    wrq.u.essid.length = strlen(ssid);
    wrq.u.essid.flags = 1;
    if(ioctl(fd, SIOCSIWESSID, &wrq) < 0) {
        rc = -errno;
        close(fd);
        return rc;
    }

    memset(&wrq, 0, sizeof(wrq));
    snprintf(wrq.ifr_name, IFNAMSIZ, "%s", ifname);
    wrq.u.encoding.flags = IW_ENCODE_DISABLED;
    wrq.u.encoding.pointer = NULL;
    wrq.u.encoding.length = 0;
    (void)ioctl(fd, SIOCSIWENCODE, &wrq);
    close(fd);
    return 0;
}

#else /* host render harness: deterministic, clearly simulated networks */

int vqeaf_wifi_scan(vqeaf_wifi_net_t *out, int max)
{
    static const vqeaf_wifi_net_t sample[] = {
        { "TOTOLINK_A720R", "A4:2B:B0:11:22:33", 78, 1 },
        { "TOTOLINK_A720R_5G", "A4:2B:B0:11:22:34", 61, 1 },
        { "QEAF-GUEST", "A4:2B:B0:11:22:35", 34, 0 },
    };

    if(out == NULL || max <= 0) return -EINVAL;
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    int count = (int)(sizeof(sample) / sizeof(sample[0]));
    if(count > max) count = max;
    for(int i = 0; i < count; i++) out[i] = sample[i];
    return count;
}

int vqeaf_wifi_connect(const char *ssid, const char *password)
{
    (void)ssid;
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    if(password != NULL && password[0] != '\0') return -EOPNOTSUPP;
    return 0;
}

#endif

const char *vqeaf_wifi_error(int rc)
{
    switch(rc) {
    case -ENODEV:     return "Không có giao diện mạng (thiếu driver/firmware).";
    case -EOPNOTSUPP: return "Mạng bảo mật cần supplicant WPA; ảnh này chưa có.";
    case -EAGAIN:     return "Chưa có kết quả quét; thử Quét lại.";
    case -EINVAL:     return "Tên mạng không hợp lệ.";
    case -EPERM:      return "Không đủ quyền điều khiển Wi-Fi.";
    default:          return "Không kết nối được (lỗi driver).";
    }
}
