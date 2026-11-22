/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_FIRMWARE_BOOT_H
#define VQEAF_FIRMWARE_BOOT_H
#include <stdint.h>
#include <stddef.h>

/* WCN3990 firmware boot state machine. A state advances only when the
 * platform callback confirms it from real hardware/firmware state. No fixed
 * sleep is treated as proof that firmware booted. */
typedef enum {
    VQEAF_FW_OFF = 0,
    VQEAF_FW_POWER_ON,
    VQEAF_FW_CLOCK_READY,
    VQEAF_FW_BUS_READY,
    VQEAF_FW_VALIDATED,
    VQEAF_FW_LOADING,
    VQEAF_FW_BOOTING,
    VQEAF_FW_QRTR_READY,
    VQEAF_FW_QMI_DISCOVERED,
    VQEAF_FW_WLAN_READY,
    VQEAF_FW_FAILED,
    VQEAF_FW_CRASHED
} vqeaf_fw_state_t;

typedef struct {
    void *ctx;
    int  (*power_on)(void *);
    void (*power_off)(void *);
    int  (*clocks_enable)(void *);
    void (*clocks_disable)(void *);
    int  (*bus_ready)(void *);
    int  (*fw_validate)(void *, const char *path);   /* -ENOENT if absent */
    int  (*fw_load)(void *, const char *path, size_t *out_len);
    /* Must read real boot state: booted=1 only from firmware confirmation,
     * crashed=1 only from a real crash indication. */
    int  (*fw_boot_status)(void *, int *booted, int *crashed);
    int  (*qrtr_ready)(void *);
    int  (*qmi_discover)(void *, uint32_t *node, uint32_t *port);
    void (*log)(void *, const char *line);
    uint64_t (*now_ms)(void *ctx);
    void (*sleep_ms)(void *, uint32_t ms);
} vqeaf_fw_ops_t;

typedef struct {
    const vqeaf_fw_ops_t *ops;
    vqeaf_fw_state_t state;
    int last_error;
    uint32_t restarts;
    uint32_t fw_len;
    uint32_t qmi_node, qmi_port;
    char fw_path[128];
} vqeaf_fw_boot_t;

const char *vqeaf_fw_state_name(vqeaf_fw_state_t s);
void vqeaf_fw_boot_init(vqeaf_fw_boot_t *b, const vqeaf_fw_ops_t *ops);
/* Runs the full ladder. Returns 0 only at WLAN_READY, otherwise a negative
 * errno with b->state == VQEAF_FW_FAILED / VQEAF_FW_CRASHED. */
int vqeaf_fw_boot_run(vqeaf_fw_boot_t *b, const char *fw_path,
                      uint32_t boot_timeout_ms, uint32_t poll_ms,
                      uint32_t max_restarts);
void vqeaf_fw_boot_abort(vqeaf_fw_boot_t *b); /* safe shutdown / CNSS reset */

#endif
