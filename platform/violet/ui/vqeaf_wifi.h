/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_wifi.h - scan/connect seam for the Wi-Fi page.
 *
 * The UI never invents a network. Scanning uses the kernel's wireless
 * extensions on the live netdev; when the image has no netdev (no WCN3990
 * firmware) every call reports ENODEV and the page says so. A secure network
 * needs a WPA supplicant, which this initramfs does not carry, so the password
 * dialog collects the key but the connect call reports EOPNOTSUPP instead of
 * pretending the join succeeded.
 */
#ifndef VQEAF_WIFI_H
#define VQEAF_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_WIFI_MAX 16

typedef struct {
    char ssid[33];
    char bssid[18];
    int signal;   /* 0..100 */
    int secure;   /* 1 when privacy/RSN is advertised */
} vqeaf_wifi_net_t;

/* Triggers a scan on the first wlan* netdev and returns the network count,
 * or -errno. Results are deduplicated by SSID, strongest signal first. */
int vqeaf_wifi_scan(vqeaf_wifi_net_t *out, int max);

/* Open networks are joined with SIOCSIWESSID. A non-empty password returns
 * -EOPNOTSUPP because the 4-way handshake needs a supplicant. */
int vqeaf_wifi_connect(const char *ssid, const char *password);

/* Stable human-readable text for a scan/connect return code. */
const char *vqeaf_wifi_error(int rc);

#ifdef __cplusplus
}
#endif

#endif /* VQEAF_WIFI_H */
