/* Minimal PCI configuration-space access (0xCF8/0xCFC) for x86_64 QEMU.
 * Type-1 config addresses, bus 0..1, function 0. No PCIe ECAM, no bridges. */
#ifndef VQEAF_PCI_H
#define VQEAF_PCI_H
#include <stdint.h>
#define VQEAF_PCI_VENDOR_RED_HAT 0x1AF4u
#define VQEAF_PCI_VENDOR_INTEL   0x8086u
#define VQEAF_PCI_VENDOR_QEMU    0x1B36u
#define VQEAF_PCI_DEV_VIRTIO_BLK_LEGACY 0x1001u
#define VQEAF_PCI_DEV_ICH9_AHCI         0x2922u
#define VQEAF_PCI_DEV_SDHCI_PCI         0x0007u
#define VQEAF_PCI_DEV_UFS_HCI           0x0013u
typedef struct {
 uint8_t bus,slot,func,irq;
 uint16_t vendor,device;
 uint32_t bars[6];
 uint32_t bar0; /* convenience alias for bars[0] */
} VqeafPciDevice;
uint32_t vqeaf_pci_read32(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset);
void vqeaf_pci_write32(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset,uint32_t value);
uint16_t vqeaf_pci_read16(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset);
void vqeaf_pci_write16(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset,uint16_t value);
int vqeaf_pci_find(uint16_t vendor,uint16_t device,VqeafPciDevice *out);
/* Enable I/O + memory + bus master and clear INTx-disable. */
void vqeaf_pci_enable_busmaster(const VqeafPciDevice *pci);
#endif
