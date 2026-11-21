/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_WCN3990_H
#define VQEAF_WCN3990_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VQEAF_WCN3990_COMPATIBLE "qcom,wcn3990-wifi"
#define VQEAF_WCN3990_BUS_SNOC "snoc"
#define VQEAF_WCN3990_CE_COUNT 12u
#define VQEAF_WCN3990_FIRMWARE "wlanmdsp.mbn"
#define VQEAF_WCN3990_BOARD_CONFIG "WCNSS_qcom_cfg.ini"
typedef enum {
 VQEAF_WCN3990_OFF=0,VQEAF_WCN3990_PROBING,VQEAF_WCN3990_MAPPED,
 VQEAF_WCN3990_IRQ_READY,VQEAF_WCN3990_FIRMWARE_READY,
 VQEAF_WCN3990_CE_READY,VQEAF_WCN3990_WLFW_READY,
 VQEAF_WCN3990_NETDEV_READY,VQEAF_WCN3990_FAILED,VQEAF_WCN3990_REMOVED
} VqeafWcn3990State;
/* Resources come from the Violet device-tree/platform layer. */
typedef struct {
 const char *compatible; const char *bus;
 uint64_t regs_base,regs_size;
 uint32_t ce_irq_first,ce_irq_count;
 uint64_t msa_base,msa_size;
} VqeafWcn3990Resources;
typedef struct {
 void *ctx;
 int (*map_registers)(void *,uint64_t,uint64_t);
 int (*request_ce_irqs)(void *,uint32_t,uint32_t);
 /* Must load real firmware + board data/calibration; -ENODATA means missing.
  * Never synthesize factory calibration or a MAC address. */
 int (*load_firmware_and_board_data)(void *,const char *,const char *);
 int (*init_copy_engines)(void *);
 int (*start_wlfw)(void *);
 int (*register_netdev)(void *);
 void (*shutdown)(void *,VqeafWcn3990State);
} VqeafWcn3990Ops;
#define VQEAF_WCN3990_DEVICE_ABI 1u
typedef struct {
 uint32_t abi,size;
 VqeafWcn3990Resources resources;
 VqeafWcn3990Ops ops;
} VqeafWcn3990Device;
int vqeaf_wcn3990_register(void);
int vqeaf_wcn3990_bind(VqeafWcn3990Device *device,uint32_t *instance_id);
int vqeaf_wcn3990_state(void);
int vqeaf_wcn3990_last_error(void);
#ifdef __cplusplus
}
#endif
#endif
