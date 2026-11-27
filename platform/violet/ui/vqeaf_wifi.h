/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WIFI_H
#define VQEAF_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

#define VQEAF_WIFI_MAX 16

typedef enum {
    VQEAF_WIFI_ENC_OPEN = 0,
    VQEAF_WIFI_ENC_WEP,
    VQEAF_WIFI_ENC_WPA,
    VQEAF_WIFI_ENC_WPA2_PSK,
    VQEAF_WIFI_ENC_WPA_WPA2,
    VQEAF_WIFI_ENC_WPA3_SAE,
    VQEAF_WIFI_ENC_OWE,
    VQEAF_WIFI_ENC_EAP
} vqeaf_wifi_enc_t;

typedef struct {
    char ssid[33];
    char bssid[18];
    int signal;   /* 0..100 */
    int secure;
    int enc;
} vqeaf_wifi_net_t;

typedef struct {
    char state[24];       /* wpa_state: DISCONNECTED, ASSOCIATING, COMPLETED */
    char ssid[33];
    char bssid[18];
    char ip_address[46];
    int connected;        /* true only when wpa_state=COMPLETED */
} vqeaf_wifi_status_t;

/* Scan through the real wpa_supplicant control interface. */
int vqeaf_wifi_scan(vqeaf_wifi_net_t *out, int max);
/* Queue open, WPA/WPA2-PSK, WPA3-SAE or OWE. Return 0 means accepted by
 * supplicant, not that association/DHCP has completed; inspect status(). */
int vqeaf_wifi_connect(const char *ssid, const char *password, int enc);
int vqeaf_wifi_disconnect(void);
int vqeaf_wifi_get_status(vqeaf_wifi_status_t *status);
const char *vqeaf_wifi_error(int rc);
const char *vqeaf_wifi_enc_name(int enc);

#ifdef __cplusplus
}
#endif
#endif
