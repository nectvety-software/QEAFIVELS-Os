/* SPDX-License-Identifier: MIT */
/* QEAF native VirtIO 0.9 network driver -- M1: discovery and config read.
 *
 * Device: PCI 1AF4:1000 (virtio-net), legacy I/O BAR0, INTx.
 * Register/config layout is the VirtIO 0.9 spec, not a guessed address:
 *   DEVICE_FEATURES 0x00, GUEST_FEATURES 0x04, QUEUE_PFN 0x08,
 *   QUEUE_NUM 0x0C, QUEUE_SEL 0x0E, QUEUE_NOTIFY 0x10, STATUS 0x12,
 *   ISR 0x13, device-specific config at 0x14.
 * virtio_net_config: mac[6] @0, status u16 @6, max_virtqueue_pairs u16 @8,
 * mtu u16 @10 (only when VIRTIO_NET_F_MTU is negotiated; M1 does not negotiate).
 *
 * This stage fails closed: if no real PCI device answers, it logs the miss
 * and reports unavailable. It never invents a MAC or a link state.
 */
#include <stdint.h>
#include <string.h>

#include "../include/vqeaf/virtio_net.h"
#include "../include/vqeaf/pci.h"
#include "../include/vqeaf/serial.h"

#define VNET_VENDOR      0x1AF4u
#define VNET_DEVICE      0x1000u
#define VNET_IO_BASE_MASK 0xFFFFFFFCu

/* Legacy virtio-pci register offsets (I/O BAR0). */
#define REG_DEVICE_FEATURES 0x00u
#define REG_GUEST_FEATURES  0x04u
#define REG_STATUS          0x12u
#define REG_ISR             0x13u
#define REG_CONFIG          0x14u

/* VirtIO status bits (spec). */
#define STATUS_ACK        0x01u
#define STATUS_DRIVER     0x02u

static VqeafPciDevice g_pci;
static uint16_t g_io_base;
static uint8_t  g_mac[VQEAF_VNET_MAC_LEN];
static int      g_ready;
static uint32_t g_irq_count;
static uint32_t g_dev_id;

static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(port));
}

static void log_hex8(uint8_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    vqeaf_serial_char(hex[(v >> 4) & 0xFu]);
    vqeaf_serial_char(hex[v & 0xFu]);
}

static void log_u32(uint32_t v)
{
    unsigned shifts;
    int started = 0;
    for(shifts = 28; ; shifts -= 4) {
        unsigned nibble = (v >> shifts) & 0xFu;
        if(nibble || started || shifts == 0) {
            log_hex8((uint8_t)nibble);
            started = 1;
        }
        if(shifts == 0) break;
    }
}

int vqeaf_virtio_net_init(void)
{
    uint32_t features, status;
    uint16_t link;

    memset(&g_pci, 0, sizeof(g_pci));
    g_ready = 0;
    g_irq_count = 0;
    g_dev_id = 0;

    if(vqeaf_pci_find(VNET_VENDOR, VNET_DEVICE, &g_pci) != 0) {
        vqeaf_serial_write("NET: no virtio-net PCI device (1af4:1000)\r\n");
        return -19; /* -ENODEV */
    }

    /* Legacy device uses I/O BAR0; BAR bit 0 marks I/O space. */
    if(!(g_pci.bar0 & 1u)) {
        vqeaf_serial_write("NET: virtio-net BAR0 is not I/O space\r\n");
        return -22; /* -EINVAL */
    }
    g_io_base = (uint16_t)(g_pci.bar0 & VNET_IO_BASE_MASK);
    g_dev_id = (uint32_t)g_pci.device;
    vqeaf_pci_enable_busmaster(&g_pci);

    /* Spec-established handshake: acknowledge + announce driver, but do NOT
     * set DRIVER_OK until a virtqueue and IRQ handler exist (later milestones). */
    status = inb((uint16_t)(g_io_base + REG_STATUS));
    outb((uint16_t)(g_io_base + REG_STATUS),
         (uint8_t)((status | STATUS_ACK | STATUS_DRIVER) & 0xFFu));
    features = inl((uint16_t)(g_io_base + REG_DEVICE_FEATURES));

    for(unsigned i = 0; i < VQEAF_VNET_MAC_LEN; i++)
        g_mac[i] = inb((uint16_t)(g_io_base + REG_CONFIG + i));
    link = inw((uint16_t)(g_io_base + REG_CONFIG + 6u));

    g_ready = 1;
    vqeaf_serial_write("NET: virtio-net 1af4:1000 io=0x");
    log_u32(g_io_base);
    vqeaf_serial_write(" irq=");
    log_u32(g_pci.irq);
    vqeaf_serial_write(" features=0x");
    log_u32(features);
    vqeaf_serial_write(" mac=");
    for(unsigned i = 0; i < VQEAF_VNET_MAC_LEN; i++) {
        log_hex8(g_mac[i]);
        if(i + 1 < VQEAF_VNET_MAC_LEN) vqeaf_serial_char(':');
    }
    vqeaf_serial_write(" link=0x");
    log_u32(link);
    vqeaf_serial_write("\r\n");
    vqeaf_serial_write("NET: M1 discovery OK; virtqueue/IRQ/TX-RX not enabled\r\n");
    return 0;
}

int vqeaf_virtio_net_ready(void) { return g_ready; }
void vqeaf_virtio_net_mac(uint8_t out[VQEAF_VNET_MAC_LEN])
{
    memcpy(out, g_mac, VQEAF_VNET_MAC_LEN);
}
uint32_t vqeaf_virtio_net_irq_count(void) { return g_irq_count; }
uint32_t vqeaf_virtio_net_dev_id(void) { return g_dev_id; }
