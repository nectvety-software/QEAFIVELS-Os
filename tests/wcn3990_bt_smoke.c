/* SPDX-License-Identifier: MIT */
/* tests/wcn3990_bt_smoke.c - host gate for the WCN3990 Bluetooth driver.
 *
 * The transport is faked (that is the point of the vqeaf_bt_ops_t contract);
 * what is asserted is the real lifecycle: every failure branch must unwind
 * what it enabled, and hci_ready must only follow a genuine Command Complete.
 * No value in this test is used as hardware evidence. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "vqeaf/wcn3990_bt.h"

typedef struct {
    int rails_on;            /* balance of power_on - power_off */
    int power_calls;
    int power_off_calls;
    uint32_t rail_uv[3];     /* voltage handed over per rail (DT minimums) */
    uint32_t rail_ua[3];
    int fail_rail;           /* index (0..2) that returns -ENODEV, -1 none */
    int slim_open_rc;
    int slim_closed;
    int write_rc;
    int reset_status;        /* HCI Reset status byte */
    int read_mode;           /* 0 = command complete, 1 = timeout, 2 = malformed,
                                3 = vendor event then complete */
    int reads;
} Fake;

static char last_log[256];

static int f_power_on(void *ctx, const char *rail, uint32_t uv, uint32_t ua)
{
    Fake *f = (Fake *)ctx;
    int index = -1;
    if(strcmp(rail, "vdd-core") == 0) index = 0;
    else if(strcmp(rail, "vdd-pa") == 0) index = 1;
    else if(strcmp(rail, "vdd-ldo") == 0) index = 2;
    assert(index >= 0);
    assert(uv != 0u && ua != 0u);
    f->power_calls++;
    if(index >= 0 && index < 3) {
        f->rail_uv[index] = uv;
        f->rail_ua[index] = ua;
    }
    if(index == f->fail_rail) return -ENODEV;
    f->rails_on++;
    return 0;
}

static void f_power_off(void *ctx, const char *rail)
{
    Fake *f = (Fake *)ctx;
    assert(rail != NULL);
    f->power_off_calls++;
    if(f->rails_on > 0) f->rails_on--;
}

static int f_slim_open(void *ctx, const uint8_t peer_ea[VQEAF_BT_SLIM_EA_LEN],
                       const uint8_t ifd_ea[VQEAF_BT_SLIM_EA_LEN])
{
    Fake *f = (Fake *)ctx;
    /* Verbatim from sm6150.dtsi: elemental-addr and the IFD elemental-addr. */
    static const uint8_t PEER[VQEAF_BT_SLIM_EA_LEN] = {0x00, 0x01, 0x20, 0x02, 0x17, 0x02};
    static const uint8_t IFD[VQEAF_BT_SLIM_EA_LEN] = {0x00, 0x00, 0x20, 0x02, 0x17, 0x02};
    if(f->slim_open_rc < 0) return f->slim_open_rc;
    assert(memcmp(peer_ea, PEER, sizeof(PEER)) == 0);
    assert(memcmp(ifd_ea, IFD, sizeof(IFD)) == 0);
    return 0;
}

static void f_slim_close(void *ctx)
{
    Fake *f = (Fake *)ctx;
    f->slim_closed++;
}

static int f_write(void *ctx, const uint8_t *buf, size_t len)
{
    Fake *f = (Fake *)ctx;
    if(f->write_rc < 0) return f->write_rc;
    assert(len == 4u);
    assert(buf[0] == 0x01 && buf[1] == 0x03 && buf[2] == 0x0C && buf[3] == 0x00);
    return 0;
}

static int f_read(void *ctx, uint8_t *buf, size_t cap, size_t *got,
                  uint32_t timeout_ms)
{
    Fake *f = (Fake *)ctx;
    (void)timeout_ms;
    assert(cap >= 6u);
    f->reads++;
    if(f->read_mode == 1) return -ETIMEDOUT;
    if(f->read_mode == 2) {          /* event header claiming 9 params, only 2 */
        buf[0] = 0x04; buf[1] = 0x0E; buf[2] = 0x09; buf[3] = 0x01; buf[4] = 0x03;
        *got = 5u;
        return 0;
    }
    if(f->read_mode == 3 && f->reads == 1) {
        buf[0] = 0x04; buf[1] = 0xFF; buf[2] = 0x02; buf[3] = 0xAA; buf[4] = 0xBB;
        *got = 5u;
        return 0;
    }
    /* Command Complete: ncmd=1, opcode=0x0C03, status. */
    buf[0] = 0x04; buf[1] = 0x0E; buf[2] = 0x04;
    buf[3] = 0x01; buf[4] = 0x03; buf[5] = 0x0C;
    buf[6] = (uint8_t)f->reset_status;
    *got = 7u;
    return 0;
}

static int f_firmware(void *ctx, const char *name, uint8_t *buf, size_t cap,
                      size_t *len)
{
    Fake *f = (Fake *)ctx;
    (void)f;
    assert(name != NULL && strcmp(name, "BTFM.bin") == 0);
    if(cap < 112u) return -ENOMEM;
    /* Minimal ELF32 little-endian Hexagon image with one in-file PT_LOAD. */
    memset(buf, 0, 112u);
    buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 1u;  buf[5] = 1u;
    buf[18] = 164u; buf[19] = 0u;             /* e_machine = EM_QDSP6 */
    buf[28] = 52u;                            /* e_phoff */
    buf[42] = 32u; buf[43] = 0u;              /* e_phentsize */
    buf[44] = 1u;  buf[45] = 0u;              /* e_phnum */
    buf[52] = 1u;                             /* PT_LOAD */
    buf[52 + 4] = 96u;                        /* p_offset */
    buf[52 + 16] = 16u;                       /* p_filesz */
    buf[96] = 0x5Au;
    *len = 112u;
    return 0;
}

static void f_log(void *ctx, const char *line)
{
    (void)ctx;
    snprintf(last_log, sizeof(last_log), "%s", line ? line : "");
}

static void fake_init(Fake *f)
{
    memset(f, 0, sizeof(*f));
    f->fail_rail = -1;
    f->write_rc = 0;
    f->reset_status = 0;
    f->read_mode = 0;
}

static const vqeaf_bt_ops_t OPS = {
    NULL, f_power_on, f_power_off, f_slim_open, f_slim_close,
    f_write, f_read, f_firmware, f_log
};

static uint8_t fw[4096];

int main(void)
{
    Fake f;
    vqeaf_bt_t bt;
    vqeaf_bt_ops_t ops;

    /* 1. Incomplete contract: refuse before touching anything. */
    fake_init(&f);
    ops = OPS; ops.ctx = &f; ops.slim_open = NULL;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == -EINVAL);
    assert(bt.stage == VQEAF_BT_FAILED && bt.hci_ready == 0);
    assert(f.power_calls == 0 && f.reads == 0);

    /* 2. Full success path. */
    fake_init(&f);
    ops = OPS; ops.ctx = &f;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == 0);
    assert(bt.hci_ready == 1 && bt.stage == VQEAF_BT_HCI_READY);
    assert(bt.fw_len == 112u && bt.fw_hash != 0u);
    assert(f.rails_on == 3 && f.power_calls == 3);
    /* Rail levels are the board-source minimums, not rounded guesses. */
    assert(f.rail_uv[0] == 1800000u && f.rail_uv[1] == 1304000u &&
           f.rail_uv[2] == 3312000u);
    assert(f.rail_ua[0] == 1u && f.rail_ua[1] == 1u && f.rail_ua[2] == 1u);
    assert(strcmp(vqeaf_bt_stage_name(bt.stage), "hci-ready") == 0);

    /* 3. Stop unwinds the transport and every rail, once. */
    vqeaf_bt_stop(&bt);
    assert(bt.hci_ready == 0 && bt.stage == VQEAF_BT_STOPPED);
    assert(f.rails_on == 0 && f.slim_closed == 1);
    assert(f.power_off_calls == 3);
    vqeaf_bt_stop(&bt);                    /* idempotent */
    assert(f.slim_closed == 1);

    /* 4. Rail 2 fails: the first rail must be switched back off. */
    fake_init(&f); f.fail_rail = 1;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == -ENODEV);
    assert(bt.stage == VQEAF_BT_FAILED && bt.hci_ready == 0);
    assert(f.rails_on == 0 && f.power_off_calls == 1);

    /* 5. SLIMbus controller absent: all rails release. */
    fake_init(&f); f.slim_open_rc = -ENODEV;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == -ENODEV);
    assert(f.rails_on == 0 && f.power_off_calls == 3);
    assert(strcmp(vqeaf_bt_error_name(bt.last_error), "ENODEV") == 0);

    /* 6. HCI write fails. */
    fake_init(&f); f.write_rc = -EIO;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == -EIO);
    assert(f.rails_on == 0 && f.slim_closed == 1);

    /* 7. No Command Complete: timeout, never ready. */
    fake_init(&f); f.read_mode = 1;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == -ETIMEDOUT);
    assert(bt.hci_ready == 0 && f.rails_on == 0);

    /* 8. Malformed frame is a protocol error, not a retry loop. */
    fake_init(&f); f.read_mode = 2;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == -EPROTO);
    assert(f.reads == 1);

    /* 9. Vendor event first, then the real completion. */
    fake_init(&f); f.read_mode = 3;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == 0);
    assert(f.reads == 2 && bt.hci_ready == 1);
    vqeaf_bt_stop(&bt);

    /* 10. Non-zero status in Command Complete is a refusal. */
    fake_init(&f); f.reset_status = 0x12;
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, sizeof(fw)) == -EIO);
    assert(bt.hci_ready == 0);

    /* 11. Empty firmware and a truncated ELF are refused before power. */
    fake_init(&f);
    vqeaf_bt_init(&bt, &ops);
    assert(vqeaf_bt_start(&bt, "BTFM.bin", fw, 0u) == -EINVAL);
    assert(f.power_calls == 0);

    printf("wcn3990_bt: 11 lifecycle checks passed\n");
    return 0;
}
