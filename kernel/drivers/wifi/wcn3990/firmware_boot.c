/* SPDX-License-Identifier: MIT */
/*
 * firmware_boot.c — confirmed-state firmware lifecycle for WCN3990.
 * Every transition is gated by a platform callback that reads real state.
 * Never marks WLAN_READY from a timer, a fixed sleep or an assumed success.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "vqeaf/firmware_boot.h"

const char *vqeaf_fw_state_name(vqeaf_fw_state_t s)
{
    switch(s) {
    case VQEAF_FW_OFF:            return "POWER_OFF";
    case VQEAF_FW_POWER_ON:       return "POWER_ON";
    case VQEAF_FW_CLOCK_READY:    return "CLOCK_READY";
    case VQEAF_FW_BUS_READY:      return "BUS_READY";
    case VQEAF_FW_VALIDATED:      return "FIRMWARE_VALIDATED";
    case VQEAF_FW_LOADING:        return "FIRMWARE_LOADING";
    case VQEAF_FW_BOOTING:        return "FIRMWARE_BOOTING";
    case VQEAF_FW_QRTR_READY:     return "QRTR_READY";
    case VQEAF_FW_QMI_DISCOVERED: return "QMI_SERVICE_DISCOVERED";
    case VQEAF_FW_WLFW_READY:     return "WLFW_READY";
    case VQEAF_FW_WLAN_READY:     return "WLAN_READY";
    case VQEAF_FW_CRASHED:        return "FIRMWARE_CRASHED";
    default:                      return "FAILED";
    }
}

void vqeaf_fw_boot_init(vqeaf_fw_boot_t *b, const vqeaf_fw_ops_t *ops)
{
    if(b == NULL) return;
    memset(b, 0, sizeof(*b));
    b->ops = ops;
    b->state = VQEAF_FW_OFF;
}

/* Manual append: freestanding builds have no libc snprintf. */
static size_t app(char *dst, size_t cap, size_t used, const char *s)
{
    size_t n = strlen(s);
    if(n > cap - used - 1u) n = cap - used - 1u;
    memcpy(dst + used, s, n);
    return used + n;
}

static void note(vqeaf_fw_boot_t *b, const char *what)
{
    char line[192];
    size_t used = 0;
    int e = b != NULL ? b->last_error : 0;
    char enum_buf[16];
    size_t n = 0;
    if(b == NULL || b->ops == NULL || b->ops->log == NULL) return;
    used = app(line, sizeof(line), used, "fw-boot: ");
    used = app(line, sizeof(line), used, what);
    used = app(line, sizeof(line), used, " (state=");
    used = app(line, sizeof(line), used, vqeaf_fw_state_name(b->state));
    used = app(line, sizeof(line), used, " err=");
    if(e < 0) { enum_buf[n++] = '-'; e = -e; }
    do { enum_buf[n++] = (char)('0' + e % 10); e /= 10; } while(e > 0 && n < 14);
    enum_buf[n] = '\0';
    used = app(line, sizeof(line), used, enum_buf);
    used = app(line, sizeof(line), used, ")\n");
    line[used] = '\0';
    b->ops->log(b->ops->ctx, line);
}

static int fail(vqeaf_fw_boot_t *b, int rc)
{
    const vqeaf_fw_ops_t *o = b->ops;
    b->last_error = rc ? rc : -EIO;
    b->state = VQEAF_FW_FAILED;
    /* Release in reverse order; these callbacks are idempotent by contract. */
    if(o != NULL && o->clocks_disable != NULL) o->clocks_disable(o->ctx);
    if(o != NULL && o->power_off != NULL) o->power_off(o->ctx);
    note(b, "failed");
    return b->last_error;
}

static int set(vqeaf_fw_boot_t *b, vqeaf_fw_state_t s)
{
    b->state = s;
    b->last_error = 0;
    note(b, "entered");
    return 0;
}

static int wait_boot(vqeaf_fw_boot_t *b, uint32_t timeout_ms, uint32_t poll_ms)
{
    const vqeaf_fw_ops_t *o = b->ops;
    uint64_t deadline = o->now_ms(o->ctx) + timeout_ms;
    for(;;) {
        int booted = 0, crashed = 0;
        int rc = o->fw_boot_status(o->ctx, &booted, &crashed);
        if(rc) return fail(b, rc);
        if(crashed) { b->state = VQEAF_FW_CRASHED; note(b, "crash"); return -EIO; }
        if(booted) return set(b, VQEAF_FW_BOOTING);
        if(o->now_ms(o->ctx) >= deadline) return fail(b, -ETIMEDOUT);
        if(o->sleep_ms != NULL && poll_ms > 0) o->sleep_ms(o->ctx, poll_ms);
    }
}

static int wait_wlfw(vqeaf_fw_boot_t *b, uint32_t timeout_ms, uint32_t poll_ms)
{
    const vqeaf_fw_ops_t *o = b->ops;
    uint64_t deadline = o->now_ms(o->ctx) + timeout_ms;
    for(;;) {
        int rc = o->wlfw_ready(o->ctx);
        if(rc == 0) return set(b, VQEAF_FW_WLFW_READY);
        if(rc != -EAGAIN) return fail(b, rc);
        if(o->now_ms(o->ctx) >= deadline) return fail(b, -ETIMEDOUT);
        if(o->sleep_ms != NULL && poll_ms > 0) o->sleep_ms(o->ctx, poll_ms);
    }
}

int vqeaf_fw_boot_run(vqeaf_fw_boot_t *b, const char *fw_path,
                      uint32_t boot_timeout_ms, uint32_t poll_ms,
                      uint32_t wlfw_timeout_ms, uint32_t max_restarts)
{
    const vqeaf_fw_ops_t *o;
    size_t len = 0;
    int rc;

    if(b == NULL || b->ops == NULL || fw_path == NULL ||
       boot_timeout_ms == 0 || wlfw_timeout_ms == 0) return -EINVAL;
    o = b->ops;
    if(!o->power_on || !o->power_off || !o->clocks_enable ||
       !o->clocks_disable || !o->bus_ready || !o->fw_validate ||
       !o->fw_load || !o->fw_boot_status || !o->qrtr_ready ||
       !o->qmi_discover || !o->wlfw_ready || !o->now_ms) return -EINVAL;
    b->last_error = 0;
    b->restarts = 0;
    b->qmi_node = b->qmi_port = 0;
    b->fw_len = 0;
    if(strlen(fw_path) >= sizeof(b->fw_path)) return -ENAMETOOLONG;
    strcpy(b->fw_path, fw_path);

    for(;;) {
        /* POWER_OFF -> POWER_ON */
        rc = o->power_on(o->ctx);
        if(rc) return fail(b, rc);
        set(b, VQEAF_FW_POWER_ON);

        rc = o->clocks_enable(o->ctx);
        if(rc) return fail(b, rc);
        set(b, VQEAF_FW_CLOCK_READY);

        rc = o->bus_ready(o->ctx);
        if(rc) return fail(b, rc);
        set(b, VQEAF_FW_BUS_READY);

        rc = o->fw_validate(o->ctx, b->fw_path);
        if(rc) return fail(b, rc);
        set(b, VQEAF_FW_VALIDATED);

        rc = o->fw_load(o->ctx, b->fw_path, &len);
        if(rc || len == 0) return fail(b, rc ? rc : -ENODATA);
        b->fw_len = (uint32_t)len;
        set(b, VQEAF_FW_LOADING);

        rc = wait_boot(b, boot_timeout_ms, poll_ms);
        if(rc == -EIO && b->state == VQEAF_FW_CRASHED) {
            /* Crash: bounded safe restart, then report honestly. */
            o->power_off(o->ctx);
            if(b->restarts < max_restarts) {
                b->restarts++;
                note(b, "restarting after crash");
                continue;
            }
            note(b, "restart limit exhausted");
            return rc;
        }
        if(rc) return rc;

        rc = o->qrtr_ready(o->ctx);
        if(rc) return fail(b, rc);
        set(b, VQEAF_FW_QRTR_READY);

        rc = o->qmi_discover(o->ctx, &b->qmi_node, &b->qmi_port);
        if(rc) return fail(b, rc); /* -ENOENT stays FAILED, never WLAN_READY */
        set(b, VQEAF_FW_QMI_DISCOVERED);

        rc = wait_wlfw(b, wlfw_timeout_ms, poll_ms);
        if(rc) return rc;
        set(b, VQEAF_FW_WLAN_READY);
        return 0;
    }
}

void vqeaf_fw_boot_abort(vqeaf_fw_boot_t *b)
{
    const vqeaf_fw_ops_t *o;
    if(b == NULL || b->ops == NULL) return;
    o = b->ops;
    if(o->clocks_disable != NULL) o->clocks_disable(o->ctx);
    if(o->power_off != NULL) o->power_off(o->ctx);
    b->state = VQEAF_FW_OFF;
    b->last_error = 0;
    note(b, "aborted");
}
