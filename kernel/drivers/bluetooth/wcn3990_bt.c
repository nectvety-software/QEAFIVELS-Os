/* SPDX-License-Identifier: MIT */
/* kernel/drivers/bluetooth/wcn3990_bt.c - WCN3990 Bluetooth bring-up.
 *
 * Port of the fail-closed parts of the stock qca_bt / btfmslim bring-up into
 * the QEAFIVELS driver model. What is ported is the sequence and its checks,
 * not Linux code: the transport arrives through vqeaf_bt_ops_t, so this file
 * has no MMIO, no sysfs and no file paths.
 *
 * Verified on violet's board source (sm6150.dtsi): the function is
 * "qca,wcn3990" behind /soc/slim@62e40000/wcn3990 ("qcom,btfmslim_slave",
 * controller "qcom,slim-ngd") with three pm6150l regulator supplies.
 * NOT verified on the handset: BT_EN wiring, SLIMbus access from a non-stock
 * kernel, and calibration. Nothing here is marked ready without a real HCI
 * Command Complete, and the caller is told why it failed. */
#include "vqeaf/wcn3990_bt.h"

#include <errno.h>
#include <string.h>

/* btfmslim_slave elemental addresses, byte for byte from sm6150.dtsi:
 * elemental-addr and qcom,btfm-slim-ifd-elemental-addr. */
static const uint8_t SLIM_PEER_EA[VQEAF_BT_SLIM_EA_LEN] = {0x00, 0x01, 0x20, 0x02, 0x17, 0x02};
static const uint8_t SLIM_IFD_EA[VQEAF_BT_SLIM_EA_LEN] = {0x00, 0x00, 0x20, 0x02, 0x17, 0x02};

#define BT_HCI_CMD_PKT  0x01u
#define BT_HCI_EVT_PKT  0x04u
#define BT_OPCODE_RESET 0x0C03u
#define BT_EVT_CMD_COMPLETE 0x0Eu
#define BT_RESET_TIMEOUT_MS 2000u
#define BT_READ_ATTEMPTS 8

static const char *const RAILS[3] = {"vdd-core", "vdd-pa", "vdd-ldo"};
static const uint32_t RAIL_UV[3] = {VQEAF_BT_VDD_CORE_UV, VQEAF_BT_VDD_PA_UV,
                                    VQEAF_BT_VDD_LDO_UV};
static const uint32_t RAIL_UA[3] = {VQEAF_BT_VDD_CORE_UA, VQEAF_BT_VDD_PA_UA,
                                    VQEAF_BT_VDD_LDO_UA};

static void note(vqeaf_bt_t *bt, const char *text)
{
    size_t i = 0;
    if(bt == NULL) return;
    if(text == NULL) text = "";
    while(text[i] != '\0' && i + 1u < sizeof(bt->note)) {
        bt->note[i] = text[i];
        i++;
    }
    bt->note[i] = '\0';
    if(bt->ops != NULL && bt->ops->log != NULL) bt->ops->log(bt->ops->ctx, text);
}

static int have_contract(const vqeaf_bt_ops_t *ops)
{
    if(ops == NULL) return 0;
    return ops->power_on != NULL && ops->power_off != NULL &&
           ops->slim_open != NULL && ops->slim_close != NULL &&
           ops->write != NULL && ops->read != NULL &&
           ops->firmware_read != NULL;
}

static uint32_t fnv1a(const uint8_t *data, size_t len)
{
    uint32_t h = 2166136261u;
    size_t i;
    for(i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Container check before the image could reach the hardware. A truncated ELF
 * is refused; an image whose container is not ELF is accepted as a raw image
 * and reported as "container not identified" instead of being guessed. */
static int check_container(const uint8_t *data, size_t len, const char **what)
{
    if(len < 4u) { *what = "too small"; return -EINVAL; }
    if(!(data[0] == 0x7Fu && data[1] == 'E' && data[2] == 'L' && data[3] == 'F')) {
        *what = "raw image (container not identified)";
        return 0;
    }
    if(len < 52u) { *what = "truncated ELF header"; return -EINVAL; }
    if(data[4] != 1u || data[5] != 1u) { *what = "not ELF32 little-endian"; return -EINVAL; }
    if(rd16(data + 18) != 164u) { *what = "not a Hexagon image"; return -EINVAL; }
    {
        uint32_t phoff = rd32(data + 28);
        uint16_t phentsize = rd16(data + 42);
        uint16_t phnum = rd16(data + 44);
        uint16_t i;
        int loads = 0;
        if(phentsize < 32u || phnum == 0u || phnum > 32u) { *what = "bad program headers"; return -EINVAL; }
        for(i = 0; i < phnum; i++) {
            size_t at = (size_t)phoff + (size_t)i * phentsize;
            uint32_t off, filesz;
            if(at + 32u > len) { *what = "program header outside file"; return -EINVAL; }
            if(rd32(data + at) != 1u) continue;  /* PT_LOAD */
            off = rd32(data + at + 4);
            filesz = rd32(data + at + 16);
            if(filesz == 0u) continue;
            if((size_t)off + filesz > len) { *what = "segment outside file"; return -EINVAL; }
            loads++;
        }
        if(loads == 0) { *what = "no loadable segment"; return -EINVAL; }
    }
    *what = "elf32 Hexagon, segments inside file";
    return 0;
}

void vqeaf_bt_init(vqeaf_bt_t *bt, const vqeaf_bt_ops_t *ops)
{
    if(bt == NULL) return;
    memset(bt, 0, sizeof(*bt));
    bt->ops = ops;
    bt->stage = VQEAF_BT_RESET;
    note(bt, "bt: idle");
}

static void rails_off(vqeaf_bt_t *bt)
{
    int i;
    for(i = bt->rails_on - 1; i >= 0; i--) {
        bt->ops->power_off(bt->ops->ctx, RAILS[i]);
        bt->rails_on = i;
    }
}

static int power_all(vqeaf_bt_t *bt)
{
    int i;
    for(i = 0; i < 3; i++) {
        int rc = bt->ops->power_on(bt->ops->ctx, RAILS[i], RAIL_UV[i], RAIL_UA[i]);
        if(rc < 0) {
            note(bt, "bt: rail enable failed");
            bt->last_error = rc;
            return rc;
        }
        bt->rails_on = i + 1;
    }
    return 0;
}

/* Looks for the Command Complete of HCI Reset in the stream. Vendor events
 * before it are skipped, never treated as success. */
static int wait_reset_complete(vqeaf_bt_t *bt)
{
    int attempt;
    for(attempt = 0; attempt < BT_READ_ATTEMPTS; attempt++) {
        uint8_t frame[80];
        size_t got = 0;
        int rc = bt->ops->read(bt->ops->ctx, frame, sizeof(frame), &got,
                               BT_RESET_TIMEOUT_MS);
        size_t at = 0;
        if(rc == -ETIMEDOUT) continue;
        if(rc < 0) { bt->last_error = rc; note(bt, "bt: hci read failed"); return rc; }
        if(got == 0u) continue;
        while(at + 3u <= got) {
            uint8_t plen = frame[at + 2];
            if(at + 3u + plen > got) break;            /* truncated frame */
            /* Command Complete params: ncmd(1), opcode(2), status(1). The opcode
             * therefore starts one byte later than the params base. */
            if(frame[at] == BT_HCI_EVT_PKT && frame[at + 1] == BT_EVT_CMD_COMPLETE &&
               plen >= 4u && rd16(frame + at + 4) == BT_OPCODE_RESET) {
                uint8_t status = frame[at + 6];
                if(status != 0u) {
                    bt->last_error = -EIO;
                    note(bt, "bt: hci reset refused");
                    return -EIO;
                }
                return 0;
            }
            at += 3u + plen;
        }
        /* An unrelated or malformed prefix is a real protocol error, not a
         * reason to spin: only a clean timeout is retried above. */
        if(at < got) { bt->last_error = -EPROTO; note(bt, "bt: malformed hci frame"); return -EPROTO; }
    }
    bt->last_error = -ETIMEDOUT;
    note(bt, "bt: no hci reset answer");
    return -ETIMEDOUT;
}

int vqeaf_bt_start(vqeaf_bt_t *bt, const char *fw_name,
                   uint8_t *fw_buf, size_t fw_cap)
{
    const uint8_t reset_cmd[4] = {BT_HCI_CMD_PKT, 0x03, 0x0C, 0x00};
    const char *what = "not read";
    size_t len = 0;
    int rc;

    if(bt == NULL) return -EINVAL;
    if(!have_contract(bt->ops)) {
        bt->last_error = -EINVAL;
        bt->stage = VQEAF_BT_FAILED;
        note(bt, "bt: contract incomplete");
        return -EINVAL;
    }
    if(fw_name == NULL || fw_buf == NULL || fw_cap == 0u) {
        bt->last_error = -EINVAL;
        bt->stage = VQEAF_BT_FAILED;
        note(bt, "bt: no firmware buffer");
        return -EINVAL;
    }

    note(bt, "bt: loading BTFM.bin");
    rc = bt->ops->firmware_read(bt->ops->ctx, fw_name, fw_buf, fw_cap, &len);
    if(rc < 0) {
        bt->last_error = rc;
        bt->stage = VQEAF_BT_FAILED;
        note(bt, "bt: firmware unavailable");
        return rc;
    }
    if(len == 0u) {
        bt->last_error = -ENODATA;
        bt->stage = VQEAF_BT_FAILED;
        note(bt, "bt: firmware empty");
        return -ENODATA;
    }
    rc = check_container(fw_buf, len, &what);
    if(rc < 0) {
        bt->last_error = rc;
        bt->stage = VQEAF_BT_FAILED;
        note(bt, "bt: firmware container rejected");
        return rc;
    }
    bt->fw_len = (uint32_t)len;
    bt->fw_hash = fnv1a(fw_buf, len);
    bt->stage = VQEAF_BT_FW_LOADED;
    note(bt, "bt: firmware checked");

    note(bt, "bt: powering vdd-core, vdd-pa, vdd-ldo");
    rc = power_all(bt);
    if(rc < 0) {
        rails_off(bt);
        bt->stage = VQEAF_BT_FAILED;
        return rc;
    }
    bt->stage = VQEAF_BT_POWERED;

    note(bt, "bt: attaching btfmslim_slave");
    rc = bt->ops->slim_open(bt->ops->ctx, SLIM_PEER_EA, SLIM_IFD_EA);
    if(rc < 0) {
        bt->last_error = rc;
        rails_off(bt);
        bt->stage = VQEAF_BT_FAILED;
        note(bt, "bt: slim transport unavailable");
        return rc;
    }
    bt->stage = VQEAF_BT_TRANSPORT;

    note(bt, "bt: hci reset");
    rc = bt->ops->write(bt->ops->ctx, reset_cmd, sizeof(reset_cmd));
    if(rc < 0) {
        bt->last_error = rc;
        note(bt, "bt: hci write failed");
    } else {
        rc = wait_reset_complete(bt);
    }
    if(rc < 0) {
        bt->ops->slim_close(bt->ops->ctx);
        rails_off(bt);
        bt->stage = VQEAF_BT_FAILED;
        return rc;
    }

    bt->hci_ready = 1;
    bt->stage = VQEAF_BT_HCI_READY;
    bt->last_error = 0;
    note(bt, "bt: hci ready");
    return 0;
}

void vqeaf_bt_stop(vqeaf_bt_t *bt)
{
    if(bt == NULL || bt->ops == NULL) return;
    if(bt->stage == VQEAF_BT_TRANSPORT || bt->stage == VQEAF_BT_HCI_READY) {
        if(bt->ops->slim_close != NULL) bt->ops->slim_close(bt->ops->ctx);
    }
    if(bt->ops->power_off != NULL) rails_off(bt);
    bt->hci_ready = 0;
    bt->stage = VQEAF_BT_STOPPED;
    note(bt, "bt: stopped");
}

const char *vqeaf_bt_stage_name(vqeaf_bt_stage_t stage)
{
    switch(stage) {
    case VQEAF_BT_RESET: return "reset";
    case VQEAF_BT_FW_LOADED: return "fw-loaded";
    case VQEAF_BT_POWERED: return "powered";
    case VQEAF_BT_TRANSPORT: return "transport";
    case VQEAF_BT_HCI_READY: return "hci-ready";
    case VQEAF_BT_STOPPED: return "stopped";
    case VQEAF_BT_FAILED: return "failed";
    }
    return "?";
}

const char *vqeaf_bt_error_name(int err)
{
    switch(err) {
    case 0: return "none";
    case -EINVAL: return "EINVAL";
    case -ENODATA: return "ENODATA";
    case -ENODEV: return "ENODEV";
    case -ETIMEDOUT: return "ETIMEDOUT";
    case -EIO: return "EIO";
    case -EPROTO: return "EPROTO";
    case -ENOMEM: return "ENOMEM";
    }
    return "errno";
}
