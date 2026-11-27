/* SPDX-License-Identifier: MIT */
/* kernel/include/vqeaf/wcn3990_bt.h - WCN3990 Bluetooth on violet.
 *
 * Evidence used (no value here comes from another phone). The board source is
 * platform/violet/device-tree/sources/guaiyihu-sm6150/arch/arm64/boot/dts/qcom/sm6150.dtsi:
 *   - node bluetooth: bt_wcn3990, compatible "qca,wcn3990", three supplies
 *     pm6150l_l1/l2/l10 (qca,bt-vdd-core/pa/ldo-supply) with the
 *     voltage-level min/max and current-level pairs below.
 *   - node slim_qca: slim@62e40000, compatible "qcom,slim-ngd",
 *     reg "slimbus_physical" 0x62e40000+0x2c000 and "slimbus_bam_physical"
 *     0x62e04000+0x20000, interrupts GIC SPI 291/292, status "ok".
 *   - child wcn3990: compatible "qcom,btfmslim_slave",
 *     elemental-addr [00 01 20 02 17 02] and
 *     qcom,btfm-slim-ifd-elemental-addr [00 00 20 02 17 02].
 *   - build/violet/violet-sm6150-abl.dtb (live ABL tree) carries the Wi-Fi
 *     node but not this Bluetooth node, so the SLIMbus path is only known
 *     from the board source; it is NOT yet measured on the handset.
 *
 * NOT verified on hardware: that the stock kernel's btfmslim exposes these
 * addresses to a userspace/other-kernel client, the BT_EN wiring, and the
 * calibration files. The driver therefore never reports hci_ready without a
 * real HCI Command Complete frame.
 *
 * The transport (SLIMbus controller, regulators, firmware storage) is supplied
 * by the platform through vqeaf_bt_ops_t, so this file stays free of MMIO,
 * sysfs and file paths and runs unchanged in host tests. */
#ifndef VQEAF_WCN3990_BT_H
#define VQEAF_WCN3990_BT_H

#include <stdint.h>
#include <stddef.h>

/* Regulator levels from the board source (uV, uA). Each voltage-level property
 * is a <min max> pair; the driver programs the minimum, which is what the
 * stock qca_bt driver does for the initial power-up. */
#define VQEAF_BT_VDD_CORE_UV   1800000u  /* 1800000 1900000 */
#define VQEAF_BT_VDD_CORE_UA         1u  /* LPM/PFM */
#define VQEAF_BT_VDD_PA_UV     1304000u  /* 1304000 1370000 */
#define VQEAF_BT_VDD_PA_UA           1u  /* LPM/PFM */
#define VQEAF_BT_VDD_LDO_UV    3312000u  /* 3312000 3400000 */
#define VQEAF_BT_VDD_LDO_UA          1u  /* LPM/PFM */

/* btfmslim_slave elemental addresses, verbatim from the board source. Both are
 * six bytes, least significant byte first, as the SLIMbus enumerator uses them:
 * `elemental-addr` is the codec (btfmslim_slave) address and
 * `qcom,btfm-slim-ifd-elemental-addr` is the interface-device address that
 * carries the HCI channel. Keep both so the platform hands them to a
 * controller without re-deriving them. */
#define VQEAF_BT_SLIM_EA_LEN 6

typedef struct {
    void *ctx;
    /* Enable one regulator rail (name is one of the three supplies above). */
    int  (*power_on)(void *ctx, const char *rail, uint32_t uv, uint32_t ua);
    void (*power_off)(void *ctx, const char *rail);
    /* Attach to the WCN3990 Bluetooth function on the SLIMbus controller.
     * `peer_ea` is the btfmslim_slave elemental address this side sends to;
     * `ifd_ea` is the interface-device address whose HCI channel is read. */
    int  (*slim_open)(void *ctx, const uint8_t peer_ea[VQEAF_BT_SLIM_EA_LEN],
                      const uint8_t ifd_ea[VQEAF_BT_SLIM_EA_LEN]);
    void (*slim_close)(void *ctx);
    /* HCI frames carried over the SLIMbus logical channel. */
    int  (*write)(void *ctx, const uint8_t *buf, size_t len);
    int  (*read)(void *ctx, uint8_t *buf, size_t cap, size_t *got,
                 uint32_t timeout_ms);
    /* Firmware image (BTFM.bin) from the platform's read-only source. */
    int  (*firmware_read)(void *ctx, const char *name,
                          uint8_t *buf, size_t cap, size_t *len);
    void (*log)(void *ctx, const char *line);
} vqeaf_bt_ops_t;

typedef enum {
    VQEAF_BT_RESET = 0,       /* nothing touched */
    VQEAF_BT_FW_LOADED,       /* BTFM.bin read and container-checked */
    VQEAF_BT_POWERED,         /* all three rails on */
    VQEAF_BT_TRANSPORT,       /* SLIMbus function attached */
    VQEAF_BT_HCI_READY,       /* HCI Reset answered with status 0 */
    VQEAF_BT_STOPPED,
    VQEAF_BT_FAILED
} vqeaf_bt_stage_t;

typedef struct {
    const vqeaf_bt_ops_t *ops;
    vqeaf_bt_stage_t stage;
    int      last_error;      /* negative errno of the first failure */
    uint32_t fw_len;
    uint32_t fw_hash;         /* FNV-1a over BTFM.bin; 0 when not loaded */
    int      rails_on;        /* count of rails still enabled */
    int      hci_ready;       /* only after a real Command Complete */
    char     note[96];        /* last step, for bt state */
} vqeaf_bt_t;

void vqeaf_bt_init(vqeaf_bt_t *bt, const vqeaf_bt_ops_t *ops);

/* Reads BTFM.bin, checks its container, powers the three rails, attaches the
 * SLIMbus function, sends HCI Reset and waits for Command Complete.
 * Returns 0 only when HCI answered with status 0; every failure returns a
 * negative errno, leaves hci_ready at 0 and unwinds what it enabled. */
int vqeaf_bt_start(vqeaf_bt_t *bt, const char *fw_name,
                   uint8_t *fw_buf, size_t fw_cap);

/* Releases the transport and every rail that is still on. Idempotent. */
void vqeaf_bt_stop(vqeaf_bt_t *bt);

const char *vqeaf_bt_stage_name(vqeaf_bt_stage_t stage);
const char *vqeaf_bt_error_name(int err);

#endif
