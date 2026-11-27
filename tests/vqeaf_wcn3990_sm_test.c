/* SPDX-License-Identifier: MIT */
/* Unit test for the phase2 state machine's rollback order.
 * The ops below are test doubles: they record call order only. They do NOT
 * represent real hardware, and no test result here claims hardware success. */
#include "vqeaf_assert.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vqeaf/wcn3990_phase2.h"

static char log[256];
static size_t log_len;
static int fail_at;          /* 4 = msa_prepare, 5 = third request_irq, 6 = map EAGAIN */
static int irq_seen;
static int retry_map;        /* map_snoc returns -EAGAIN once before success */
static int eagain_seen;
static int map_calls, freeze_delay;
static int map_jump, delay_seen;
static uint64_t fake_now, map_jump_to;
static uint8_t ring[64 * 1024];

static void put(char c)
{
    if(log_len + 1u < sizeof(log)) log[log_len++] = c;
}

static uint64_t t_now(void *c) { (void)c; return fake_now; }
static void t_delay(void *c, uint32_t ms)
{ (void)c; if(!freeze_delay) fake_now += ms; delay_seen++; }

static int t_map(void *c, uint64_t a, uint64_t b)
{
    (void)c;(void)a;(void)b;
    put('M');
    map_calls++;
    if(map_jump) { fake_now = map_jump_to; map_jump = 0; }
    if(fail_at == 6) return -EAGAIN;          /* never ready */
    if(retry_map && ++eagain_seen == 1) return -EAGAIN; /* ready on retry */
    return 0;
}
static void t_unmap(void *c) { (void)c; put('U'); }
static int t_reg_on(void *c) { (void)c; put('R'); return 0; }
static void t_reg_off(void *c) { (void)c; put('r'); }
static int t_clk_on(void *c) { (void)c; put('C'); return 0; }
static void t_clk_off(void *c) { (void)c; put('c'); }
static int t_pwr_on(void *c) { (void)c; put('P'); return 0; }
static void t_pwr_off(void *c) { (void)c; put('p'); }
static int t_msa(void *c, uint64_t a, uint64_t b)
{ (void)c;(void)a;(void)b; put('S'); return fail_at == 4 ? -EIO : 0; }
static void t_msa_rel(void *c) { (void)c; put('s'); }
static int t_irq(void *c, uint32_t irq, uint8_t id)
{
    (void)c;(void)irq;(void)id;
    put('I'); irq_seen++;
    return (fail_at == 5 && irq_seen == 3) ? -EIO : 0;
}
static void t_irq_free(void *c, uint32_t irq, uint8_t id)
{ (void)c;(void)irq;(void)id; put('i'); }
static int t_dma(void *c, uint64_t bytes, uint64_t align,
                 void **cpu, uint64_t *dma)
{
    (void)c;(void)align;
    *cpu = ring; *dma = 0x1000u;
    (void)bytes;
    return 0;
}
static void t_dma_free(void *c, uint64_t bytes, void *cpu, uint64_t dma)
{ (void)c;(void)bytes;(void)cpu;(void)dma; }
static int t_ce(void *c, const VqeafWcn3990CeConfig *cfg, uint64_t s, uint64_t d)
{ (void)c;(void)cfg;(void)s;(void)d; return 0; }
static int t_ce_off(void *c, uint8_t id) { (void)c;(void)id; return 0; }

static VqeafWcn3990P2Device make_device(void)
{
    VqeafWcn3990P2Device d;
    uint32_t i;
    memset(&d, 0, sizeof(d));
    d.abi = VQEAF_WCN3990_P2_ABI;
    d.size = sizeof(d);
    d.resources.snoc_base = 0x18800000ull;
    d.resources.snoc_size = 0x800000ull;
    d.resources.msa_base = 0x93300000ull;
    d.resources.msa_size = 0x200000ull;
    d.resources.ce_irq_count = VQEAF_WCN3990_CE_MAX;
    for(i = 0; i < VQEAF_WCN3990_CE_MAX; i++) d.resources.ce_irq[i] = 414u + i;
    d.ops.map_snoc = t_map; d.ops.unmap_snoc = t_unmap;
    d.ops.regulators_enable = t_reg_on; d.ops.regulators_disable = t_reg_off;
    d.ops.clocks_enable = t_clk_on; d.ops.clocks_disable = t_clk_off;
    d.ops.icnss_power_on = t_pwr_on; d.ops.icnss_power_off = t_pwr_off;
    d.ops.msa_prepare = t_msa; d.ops.msa_release = t_msa_rel;
    d.ops.request_irq = t_irq; d.ops.free_irq = t_irq_free;
    d.ops.dma_alloc = t_dma; d.ops.dma_free = t_dma_free;
    d.ops.ce_configure = t_ce; d.ops.ce_disable = t_ce_off;
    return d;
}

static void check(int at, const char *expected)
{
    VqeafWcn3990P2Device d = make_device();
    uint32_t id = 0;
    log_len = 0; irq_seen = 0; fail_at = at;
    retry_map = 0; eagain_seen = 0; fake_now = 0;
    map_calls = 0; freeze_delay = 0; map_jump = 0; delay_seen = 0;
    memset(log, 0, sizeof(log));
    assert(vqeaf_wcn3990_phase2_bind(&d, &id) == -EIO);
    assert(vqeaf_wcn3990_phase2_state() == VQEAF_WCN_P2_FAILED);
    assert(vqeaf_wcn3990_phase2_error() == -EIO);
    log[log_len] = 0;
    if(strcmp(log, expected) != 0) {
        printf("rollback mismatch: got '%s' expected '%s'\n", log, expected);
        assert(0);
    }
    printf("PASS fail_at=%d rollback='%s'\n", at, log);
}

static VqeafWcn3990P2Device timed_device(void)
{
    VqeafWcn3990P2Device d = make_device();
    d.step_timeout_ms = 3;
    d.ops.now_ms = t_now;
    d.ops.delay_ms = t_delay;
    return d;
}

static void check_timeout(void)
{
    VqeafWcn3990P2Device d = timed_device();
    uint32_t id = 0;
    log_len = 0; fail_at = 6; retry_map = 0;
    eagain_seen = 0; fake_now = 0;
    map_jump = 0; delay_seen = 0;
    memset(log, 0, sizeof(log));
    assert(vqeaf_wcn3990_phase2_bind(&d, &id) == -ETIMEDOUT);
    assert(vqeaf_wcn3990_phase2_state() == VQEAF_WCN_P2_FAILED);
    assert(vqeaf_wcn3990_phase2_error() == -ETIMEDOUT);
    log[log_len] = 0;
    assert(strcmp(log, "MMM") == 0); /* 3 bounded attempts, then stop */
    printf("PASS timeout: -ETIMEDOUT after 3 bounded retries\n");
}

static void check_retry_then_fail(void)
{
    VqeafWcn3990P2Device d = timed_device();
    uint32_t id = 0;
    log_len = 0; fail_at = 4; retry_map = 1;
    eagain_seen = 0; fake_now = 0;
    map_jump = 0; delay_seen = 0;
    memset(log, 0, sizeof(log));
    assert(vqeaf_wcn3990_phase2_bind(&d, &id) == -EIO);
    log[log_len] = 0;
    assert(strcmp(log, "MMRCPSpcrU") == 0);
    printf("PASS retry: -EAGAIN once then bounded failure\n");
}

static void check_timeout_after_clock_jump(void)
{
    VqeafWcn3990P2Device d = timed_device();
    uint32_t id = 0;
    log_len = 0; fail_at = 6; retry_map = 0;
    eagain_seen = 0; fake_now = 0; map_jump = 1; map_jump_to = 3; delay_seen = 0;
    memset(log, 0, sizeof(log));
    assert(vqeaf_wcn3990_phase2_bind(&d, &id) == -ETIMEDOUT);
    log[log_len] = 0;
    assert(strcmp(log, "M") == 0);
    assert(delay_seen == 0);
    printf("PASS timeout: one unconditional attempt after clock jump\n");
}

static void check_timeout_clock_wrap(void)
{
    VqeafWcn3990P2Device d = timed_device();
    uint32_t id = 0;
    log_len = 0; fail_at = 6; retry_map = 0;
    eagain_seen = 0; fake_now = UINT64_MAX - 1u; map_jump = 0; delay_seen = 0;
    memset(log, 0, sizeof(log));
    assert(vqeaf_wcn3990_phase2_bind(&d, &id) == -ETIMEDOUT);
    log[log_len] = 0;
    assert(strcmp(log, "MMM") == 0);
    assert(delay_seen == 3);
    printf("PASS timeout: uint64 clock wrap remains bounded\n");
}

static void check_missing_delay_propagates_eagain(void)
{
    VqeafWcn3990P2Device d = timed_device();
    uint32_t id = 0;
    log_len = 0; fail_at = 6; retry_map = 0;
    eagain_seen = 0; fake_now = 0; map_jump = 0; delay_seen = 0;
    map_calls = 0; freeze_delay = 0;
    d.ops.delay_ms = 0;
    memset(log, 0, sizeof(log));
    assert(vqeaf_wcn3990_phase2_bind(&d, &id) == -EAGAIN);
    log[log_len] = 0;
    assert(strcmp(log, "M") == 0);
    assert(map_calls == 1 && delay_seen == 0);
    printf("PASS timeout: missing delay propagates -EAGAIN\n");
}

static void check_frozen_clock(void)
{
    VqeafWcn3990P2Device d = timed_device();
    uint32_t id = 0;
    log_len = 0; fail_at = 6; retry_map = 0;
    eagain_seen = 0; fake_now = 0; map_jump = 0; delay_seen = 0;
    map_calls = 0; freeze_delay = 1;
    memset(log, 0, sizeof(log));
    assert(vqeaf_wcn3990_phase2_bind(&d, &id) == -ETIMEDOUT);
    assert(vqeaf_wcn3990_phase2_error() == -ETIMEDOUT);
    assert(map_calls == 1 && delay_seen == 1);
    printf("PASS timeout: frozen clock stops after one delayed attempt\n");
}

static void check_attempt_cap(void)
{
    VqeafWcn3990P2Device d = timed_device();
    uint32_t id = 0;
    d.step_timeout_ms = UINT32_MAX;
    log_len = 0; fail_at = 6; retry_map = 0;
    eagain_seen = 0; fake_now = 0; map_jump = 0; delay_seen = 0;
    map_calls = 0; freeze_delay = 0;
    memset(log, 0, sizeof(log));
    assert(vqeaf_wcn3990_phase2_bind(&d, &id) == -ETIMEDOUT);
    assert(map_calls == VQEAF_WCN3990_P2_MAX_STEP_ATTEMPTS);
    assert(delay_seen == VQEAF_WCN3990_P2_MAX_STEP_ATTEMPTS - 1);
    printf("PASS timeout: attempt cap bounds a progressing clock\n");
}

int main(void)
{
    assert(vqeaf_wcn3990_phase2_register() == 0);
    check(4, "MRCPS" "p" "c" "r" "U");            /* fail before MSA */
    check(5, "MRCPS" "III" "ii" "s" "p" "c" "r" "U"); /* IRQ partial */
    check_timeout();
    check_retry_then_fail();
    check_timeout_after_clock_jump();
    check_timeout_clock_wrap();
    check_missing_delay_propagates_eagain();
    check_frozen_clock();
    check_attempt_cap();
    printf("vqeaf_wcn3990_sm: ALL OK (driver timeout PASS; backend clock UNSUPPORTED)\n");
    return 0;
}
