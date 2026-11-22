/* SPDX-License-Identifier: MIT */
#ifndef VIO_LOCD_H
#define VIO_LOCD_H
#include <stdint.h>
#include <stddef.h>

/* Native location service for QEAFIVELS OS. It reports real provider state and
 * records real client requests. It NEVER synthesizes coordinates, scan
 * results, or a "connected" network. Unknown is reported as UNKNOWN. */
typedef enum {
    LOC_MODE_OFF = 0,
    LOC_MODE_LOW_POWER,        /* network/WiFi/cellular only */
    LOC_MODE_HIGH_ACCURACY     /* GNSS + WiFi + cellular */
} loc_mode_t;

typedef enum {
    LOC_PROV_UNSUPPORTED = 0,  /* no hardware/driver on this board */
    LOC_PROV_BLOCKED,          /* present but unavailable (with reason) */
    LOC_PROV_AVAILABLE,        /* probed OK, no fix yet */
    LOC_PROV_ACTIVE            /* real fix in progress, with evidence */
} loc_provider_state_t;

typedef struct {
    loc_provider_state_t state;
    char reason[64];
} loc_provider_t;

#define LOC_MAX_REQUESTS 32u
#define LOC_CLIENT_MAX   32u
#define LOC_PURPOSE_MAX  32u

typedef struct {
    char client[LOC_CLIENT_MAX];
    char purpose[LOC_PURPOSE_MAX];
    uint64_t monotonic_ms;
    int uid;
} loc_request_t;

/* Probe real hardware state into providers. */
void locd_probe_providers(loc_provider_t *gnss, loc_provider_t *wifi,
                          loc_provider_t *cell, loc_provider_t *bt);
const char *locd_mode_name(loc_mode_t m);
const char *locd_provider_name(loc_provider_state_t s);

/* Service state (single owner, called from the Unix-socket worker). */
void locd_init(void);
int  locd_set_mode(loc_mode_t mode);
loc_mode_t locd_get_mode(void);
void locd_set_scan(int wifi, int on);
int  locd_get_scan(int wifi);
/* Records a real request. Returns the ring index, or -1 on bad input. */
int  locd_record_request(const char *client, const char *purpose, int uid);
/* Renders the full JSON-line status into out. Returns bytes or -1. */
int  locd_status_json(char *out, size_t cap);

/* Blocking Unix-socket server. Never returns unless the socket fails. */
int  locd_serve(const char *path);

#endif
