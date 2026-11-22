/* SPDX-License-Identifier: MIT */
/* Host tests for the WCN3990 firmware boot state machine (mock callbacks). */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vqeaf/firmware_boot.h"

typedef struct {
    int fail_step;      /* 1..8, 0 = none */
    int boot_mode;      /* 0 ok, 1 timeout, 2 crash once, 3 crash always */
    int qmi_found;
    uint64_t now;
    uint32_t boot_calls;
    int step;
    int calls[16];
    int ncalls;
    char log[16][128];
    int nlog;
} sc_t;

static void mark(sc_t *s, int n) { s->step = n; if(s->ncalls < 16) s->calls[s->ncalls++] = n; }
static int  failed(sc_t *s, int n) { mark(s, n); return s->fail_step == n ? -EIO : 0; }
static uint64_t now(void *p) { return ((sc_t *)p)->now; }
static void sleep_ms(void *p, uint32_t ms) { ((sc_t *)p)->now += ms; }
static void logline(void *p, const char *line)
{
    sc_t *s = (sc_t *)p;
    if(s->nlog < 16) { snprintf(s->log[s->nlog], sizeof(s->log[0]), "%s", line); s->nlog++; }
}
static int power_on(void *p) { return failed((sc_t *)p, 1); }
static void power_off(void *p) { mark((sc_t *)p, 90); }
static int clocks(void *p) { return failed((sc_t *)p, 2); }
static void clocks_off(void *p) { mark((sc_t *)p, 91); }
static int bus(void *p) { return failed((sc_t *)p, 3); }
static int validate(void *p, const char *path)
{ assert(strcmp(path, "wlanmdsp.mbn") == 0); return failed((sc_t *)p, 4); }
static int load(void *p, const char *path, size_t *len)
{ assert(strcmp(path, "wlanmdsp.mbn") == 0); *len = 4096; return failed((sc_t *)p, 5); }
static int boot_status(void *p, int *booted, int *crashed)
{
    sc_t *s = (sc_t *)p;
    s->boot_calls++;
    mark(s, 6);
    if(s->fail_step == 6) return -EIO;
    *booted = *crashed = 0;
    if(s->boot_mode == 1) return 0;                       /* never boots */
    if(s->boot_mode == 2 && s->boot_calls == 1) { *crashed = 1; return 0; }
    if(s->boot_mode == 3) { *crashed = 1; return 0; }
    *booted = 1;
    return 0;
}
static int qrtr(void *p) { return failed((sc_t *)p, 7); }
static int discover(void *p, uint32_t *node, uint32_t *port)
{
    sc_t *s = (sc_t *)p;
    mark(s, 8);
    if(s->fail_step == 8 || !s->qmi_found) return -ENOENT;
    *node = 2; *port = 0x1234;
    return 0;
}

static vqeaf_fw_ops_t make_ops(sc_t *s)
{
    vqeaf_fw_ops_t o;
    memset(&o, 0, sizeof(o));
    o.ctx = s;
    o.power_on = power_on; o.power_off = power_off;
    o.clocks_enable = clocks; o.clocks_disable = clocks_off;
    o.bus_ready = bus; o.fw_validate = validate; o.fw_load = load;
    o.fw_boot_status = boot_status; o.qrtr_ready = qrtr;
    o.qmi_discover = discover; o.log = logline;
    o.now_ms = now; o.sleep_ms = sleep_ms;
    return o;
}

static void init_sc(sc_t *s)
{
    memset(s, 0, sizeof(*s));
    s->boot_mode = 0;
    s->qmi_found = 1;
}

static int has_call(sc_t *s, int n)
{
    int i;
    for(i = 0; i < s->ncalls; i++) if(s->calls[i] == n) return 1;
    return 0;
}

int main(void)
{
    sc_t s;
    vqeaf_fw_ops_t o;
    vqeaf_fw_boot_t b;
    int step;

    /* Full confirmed path -> WLAN_READY in the documented order. */
    init_sc(&s);
    o = make_ops(&s);
    vqeaf_fw_boot_init(&b, &o);
    assert(vqeaf_fw_boot_run(&b, "wlanmdsp.mbn", 100, 10, 1) == 0);
    assert(b.state == VQEAF_FW_WLAN_READY && b.fw_len == 4096);
    assert(b.qmi_node == 2 && b.qmi_port == 0x1234);
    assert(s.calls[0] == 1 && has_call(&s, 6) && has_call(&s, 8));
    assert(s.nlog > 0);

    /* Fail at every stage: never advance past the failing callback. */
    for(step = 1; step <= 8; step++) {
        init_sc(&s);
        s.fail_step = step;
        o = make_ops(&s);
        vqeaf_fw_boot_init(&b, &o);
        assert(vqeaf_fw_boot_run(&b, "wlanmdsp.mbn", 100, 10, 0) < 0);
        assert(b.state == VQEAF_FW_FAILED);
        assert(b.last_error == -EIO || b.last_error == -ENOENT);
        assert(!has_call(&s, step + 1) || step == 6);
        assert(!has_call(&s, 8) || step >= 8);
    }

    /* Firmware never confirms boot: timeout, not a fake success. */
    init_sc(&s);
    s.boot_mode = 1;
    o = make_ops(&s);
    vqeaf_fw_boot_init(&b, &o);
    assert(vqeaf_fw_boot_run(&b, "wlanmdsp.mbn", 100, 10, 0) == -ETIMEDOUT);
    assert(b.state == VQEAF_FW_FAILED && s.boot_calls == 11);
    assert(!has_call(&s, 7) && !has_call(&s, 8));

    /* Crash -> bounded safe restart. */
    init_sc(&s);
    s.boot_mode = 2;
    o = make_ops(&s);
    vqeaf_fw_boot_init(&b, &o);
    assert(vqeaf_fw_boot_run(&b, "wlanmdsp.mbn", 100, 10, 1) == 0);
    assert(b.state == VQEAF_FW_WLAN_READY && b.restarts == 1);
    assert(has_call(&s, 90)); /* power_off was called before restart */

    /* Persistent crash -> exhausted restarts, never WLAN_READY. */
    init_sc(&s);
    s.boot_mode = 3;
    o = make_ops(&s);
    vqeaf_fw_boot_init(&b, &o);
    assert(vqeaf_fw_boot_run(&b, "wlanmdsp.mbn", 100, 10, 1) == -EIO);
    assert(b.state == VQEAF_FW_CRASHED && !has_call(&s, 7));

    /* WLFW 0x45 not discovered: fail at QMI step, never WLAN_READY. */
    init_sc(&s);
    s.qmi_found = 0;
    o = make_ops(&s);
    vqeaf_fw_boot_init(&b, &o);
    assert(vqeaf_fw_boot_run(&b, "wlanmdsp.mbn", 100, 10, 0) == -ENOENT);
    assert(b.state == VQEAF_FW_FAILED && b.last_error == -ENOENT);
    assert(strcmp(vqeaf_fw_state_name(VQEAF_FW_WLAN_READY), "WLAN_READY") == 0);

    /* Abort releases resources and returns to POWER_OFF. */
    init_sc(&s);
    o = make_ops(&s);
    vqeaf_fw_boot_init(&b, &o);
    vqeaf_fw_boot_abort(&b);
    assert(b.state == VQEAF_FW_OFF && has_call(&s, 90) && has_call(&s, 91));

    printf("test_firmware_boot: ALL OK\n");
    return 0;
}
