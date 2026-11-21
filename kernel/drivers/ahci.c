/* QEMU ICH9 AHCI (8086:2922) SATA block driver.
 * HBA BAR5 MMIO, one command slot and one PRDT entry per port, READ/WRITE DMA
 * EXT + IDENTIFY. Every attached ATA disk (SSTS DET=3, SIG=0x101) becomes its
 * own block device with private CLB/FIS/CT pages, so both QEMU SATA ports can
 * be exercised in one boot. Completion is polled: QEMU routes this controller
 * through MSI (INTx line 255), which this kernel does not enable yet.
 * The block-layer registration is deliberately read-only; the driver write
 * path exists and is exercised by the kernel's scratch-sector self test. */
#include <stdint.h>
#include <stddef.h>
#include "../include/vqeaf/ahci.h"
#include "../include/vqeaf/pci.h"
#include "../include/vqeaf/mmio.h"
#include "../include/vqeaf/serial.h"
#include "../include/vqeaf/memory.h"
#include "../../system/core/include/vqeaf/block.h"
#include "../../system/core/include/vqeaf/driver.h"
#include "../../system/core/include/vqeaf/capability.h"
#define AHCI_CAP         0x00u
#define AHCI_GHC         0x04u
#define AHCI_PI          0x0Cu
#define AHCI_PORT_BASE   0x100u
#define AHCI_PORT_STRIDE 0x80u
#define PxCLB            0x00u
#define PxFB             0x08u
#define PxIS             0x10u
#define PxIE             0x14u
#define PxCMD            0x18u
#define PxTFD            0x20u
#define PxSIG            0x24u
#define PxSSTS           0x28u
#define PxSERR           0x30u
#define PxCI             0x38u
#define CMD_ST           0x1u
#define CMD_FRE          0x10u
#define CMD_CR           0x8000u
#define GHC_AE           0x80000000u
#define ATA_CMD_READ_DMA_EXT  0x25u
#define ATA_CMD_WRITE_DMA_EXT 0x35u
#define ATA_CMD_IDENTIFY      0xECu
#define SIG_ATA          0x00000101u
#define FIS_H2D          0x27u
#define AHCI_TIMEOUT     2000000u
typedef struct {
 uint32_t port;
 uint64_t mem_phys,cl_phys,fis_phys,ct_phys;
 uint64_t capacity;
 uint32_t block_id;
 struct VqeafBlockDevice dev;
 char name[8];
 uint8_t identify[512];
} AhciDisk;
static uint64_t abar;
static AhciDisk disks[VQEAF_AHCI_MAX_PORTS];
static uint32_t disk_count;
static void serial_hex(uint64_t v){
 static const char h[]="0123456789ABCDEF";
 vqeaf_serial_write("0x");for(int s=60;s>=0;s-=4)vqeaf_serial_char(h[(v>>s)&15]);
}
static uint32_t reg(uint32_t off){return vqeaf_mmio_r32(abar,off);}
static void setreg(uint32_t off,uint32_t v){vqeaf_mmio_w32(abar,off,v);}
static uint32_t preg(uint32_t p,uint32_t off){
 return vqeaf_mmio_r32(abar,AHCI_PORT_BASE+p*AHCI_PORT_STRIDE+off);
}
static void pset(uint32_t p,uint32_t off,uint32_t v){
 vqeaf_mmio_w32(abar,AHCI_PORT_BASE+p*AHCI_PORT_STRIDE+off,v);
}
static int port_wait_clear(uint32_t p,uint32_t off,uint32_t mask){
 for(uint32_t i=0;i<AHCI_TIMEOUT;i++)if(!(preg(p,off)&mask))return 0;
 return -1;
}
/* Build a 20-byte Host-to-Device register FIS and one PRDT entry. */
static void build_command(AhciDisk *d,uint8_t command,uint64_t lba,uint32_t sectors,
                          uint64_t buf_phys,int write){
 uint8_t *cf=(uint8_t*)(uintptr_t)d->ct_phys;
 uint32_t *prdt=(uint32_t*)(uintptr_t)(d->ct_phys+128u);
 uint32_t *cl=(uint32_t*)(uintptr_t)d->cl_phys;
 for(unsigned i=0;i<128u;i++)cf[i]=0;
 cf[0]=FIS_H2D;cf[1]=0x80u;cf[2]=command;cf[3]=0;
 cf[4]=(uint8_t)lba;cf[5]=(uint8_t)(lba>>8);cf[6]=(uint8_t)(lba>>16);
 cf[7]=0x40u; /* LBA mode */
 cf[8]=(uint8_t)(lba>>24);cf[9]=(uint8_t)(lba>>32);cf[10]=(uint8_t)(lba>>40);
 cf[11]=0;cf[12]=(uint8_t)sectors;cf[13]=(uint8_t)(sectors>>8);
 /* Header DW0: CFL in bits 0-4, PRDTL in bits 16-31. Bit 6 marks a host to
  * device transfer (the AHCI/EDK2 attribute layout, not the FIS-dword count
  * shifted into bits 5-9). */
 cl[0]=5u|(write?0x40u:0u)|(1u<<16);
 cl[1]=0;cl[2]=(uint32_t)d->ct_phys;cl[3]=0;cl[4]=cl[5]=cl[6]=cl[7]=0;
 prdt[0]=(uint32_t)buf_phys;prdt[1]=(uint32_t)(buf_phys>>32);
 prdt[2]=0;prdt[3]=(uint32_t)(sectors*512u-1u);
}
static int issue(AhciDisk *d){
 pset(d->port,PxIS,0xFFFFFFFFu);
 pset(d->port,PxCI,1u);
 if(port_wait_clear(d->port,PxCI,1u))return -1;
 uint32_t is=preg(d->port,PxIS),tfd=preg(d->port,PxTFD);
 pset(d->port,PxIS,is);
 if(tfd&0x00000001u)return -2; /* ERR bit */
 if(is&0x40000000u)return -3;  /* task file error */
 return 0;
}
static int identify(AhciDisk *d){
 /* QEMU ICH9 completes IDENTIFY through the command table; the PIO-setup path
  * (PxDATA) is not raised for this controller, so one PRDT entry is used. */
 build_command(d,ATA_CMD_IDENTIFY,0,1,(uint64_t)(uintptr_t)d->identify,0);
 int r=issue(d);
 if(r)return r;
 d->capacity=0;
 for(unsigned i=0;i<4u;i++)
  d->capacity|=((uint64_t)(d->identify[200+i*2]|(uint16_t)(d->identify[201+i*2]<<8)))<<(16u*i);
 if(!d->capacity){
  d->capacity=(uint64_t)(d->identify[120]|(uint16_t)(d->identify[121]<<8))|
              ((uint64_t)(d->identify[122]|(uint16_t)(d->identify[123]<<8))<<16);
 }
 return d->capacity?0:-2;
}
static int transfer(AhciDisk *d,uint8_t command,uint64_t lba,uint32_t count,uint64_t buf){
 if(!d||!count||count>64u)return -1;
 while(count){
  uint32_t chunk=count>32u?32u:count;
  if(buf+(uint64_t)chunk*512u>VQEAF_PHYS_LIMIT)return -3;
  build_command(d,command,lba,chunk,buf,command==ATA_CMD_WRITE_DMA_EXT);
  int r=issue(d);if(r)return r;
  lba+=chunk;count-=chunk;buf+=(uint64_t)chunk*512u;
 }
 return 0;
}
static int ahci_read_cb(struct VqeafBlockDevice *dev,uint64_t lba,uint32_t count,void *out){
 AhciDisk *d=(AhciDisk*)dev->ctx;
 return transfer(d,ATA_CMD_READ_DMA_EXT,lba,count,(uint64_t)(uintptr_t)out);
}
static int driver_probe(void *device){(void)device;return disk_count?0:-1;}
static int driver_remove(void *device){(void)device;return 0;}
static const VqeafDriver ahci_driver={
 VQEAF_DRIVER_ABI,sizeof(VqeafDriver),"ahci-sata",VQEAF_DEV_STORAGE,
 driver_probe,driver_remove
};
int vqeaf_ahci_init(void){
 VqeafPciDevice pci;
 disk_count=0;
 if(vqeaf_pci_find(VQEAF_PCI_VENDOR_INTEL,VQEAF_PCI_DEV_ICH9_AHCI,&pci)!=0){
  vqeaf_serial_write("AHCI: no ICH9 AHCI controller (8086:2922)\r\n");return -1;
 }
 vqeaf_pci_enable_busmaster(&pci);
 abar=vqeaf_mmio_bar(pci.bars[5]);
 if(!abar||vqeaf_mmio_map(abar,0x1000u)!=0){
  vqeaf_serial_write("AHCI: BAR5 mapping failed\r\n");return -2;
 }
 /* Enable AHCI. GHC.HR is deliberately not pulsed: firmware already brought
  * the HBA up, and QEMU leaves PxSIG at 0xFFFFFFFF after a global reset even
  * though the ports keep reporting DET=3. Per-port stop/start below re-programs
  * the command list and FIS receive area. */
 setreg(AHCI_GHC,reg(AHCI_GHC)|GHC_AE);
 uint32_t implemented=reg(AHCI_PI);
 vqeaf_serial_write("AHCI: BAR5=");serial_hex(abar);
 vqeaf_serial_write(" PI=");serial_hex(implemented);vqeaf_serial_write("\r\n");
 uint32_t found=0;
 for(uint32_t p=0;p<32u&&disk_count<VQEAF_AHCI_MAX_PORTS;p++){
  if(!(implemented&(1u<<p)))continue;
  uint32_t sig=preg(p,PxSIG),ssts=preg(p,PxSSTS);
  /* QEMU leaves PxSIG at 0xFFFFFFFF until the port is started after an HBA
   * reset, while SSTS already reports DET=3. Presence is therefore decided by
   * SSTS, and IDENTIFY confirms the attached device is really ATA. */
  if((ssts&0x0Fu)!=0x3u)continue;
  found++;
  AhciDisk *d=&disks[disk_count];
  d->port=p;
  d->mem_phys=vqeaf_pmm_alloc_pages(1u);
  if(!d->mem_phys)return -4;
  d->cl_phys=d->mem_phys;d->fis_phys=d->mem_phys+1024u;d->ct_phys=d->mem_phys+2048u;
  uint8_t *z=(uint8_t*)(uintptr_t)d->mem_phys;
  for(uint32_t i=0;i<VQEAF_PAGE_SIZE;i++)z[i]=0;
  /* Stop the port, program CLB/FB, start it again. */
  pset(p,PxCMD,preg(p,PxCMD)&~CMD_ST);
  if(port_wait_clear(p,PxCMD,CMD_CR))return -5;
  pset(p,PxCLB,(uint32_t)d->cl_phys);
  pset(p,PxCLB+4u,(uint32_t)(d->cl_phys>>32));
  pset(p,PxFB,(uint32_t)d->fis_phys);
  pset(p,PxFB+4u,(uint32_t)(d->fis_phys>>32));
  pset(p,PxSERR,0xFFFFFFFFu);pset(p,PxIS,0xFFFFFFFFu);
  pset(p,PxIE,0u);
  pset(p,PxCMD,preg(p,PxCMD)|CMD_FRE|CMD_ST);
  if(port_wait_clear(p,PxTFD,0x00000088u))return -6; /* BSY|DRQ */
  int ir=identify(d);
  if(ir){
   vqeaf_serial_write("AHCI: IDENTIFY failed on port=");serial_hex(p);
   vqeaf_serial_write(" sig=");serial_hex(sig);
   vqeaf_serial_write(" r=");serial_hex((uint64_t)(-ir));vqeaf_serial_write("\r\n");
   continue;
  }
  d->dev.name=d->name;
  for(unsigned i=0;i<sizeof d->name;i++)d->name[i]=0;
  d->name[0]='a';d->name[1]='h';d->name[2]='c';d->name[3]='i';
  d->name[4]=(char)('0'+disk_count);
  d->dev.sector_size=512u;d->dev.sector_count=d->capacity;
  d->dev.read_sectors=ahci_read_cb;d->dev.write_sectors=0;d->dev.ctx=d;
  int id=vqeaf_block_register(&d->dev);
  if(id<0)return -7;
  d->block_id=(uint32_t)id;
  disk_count++;
  vqeaf_serial_write("AHCI: port=");serial_hex(p);
  vqeaf_serial_write(" capacity=");serial_hex(d->capacity);
  vqeaf_serial_write(" block_id=");serial_hex(d->block_id);vqeaf_serial_write("\r\n");
 }
 if(!found){
  vqeaf_serial_write("AHCI: no SATA disk on any implemented port\r\n");return -8;
 }
 if(!disk_count)return -9;
 if(vqeaf_driver_register(&ahci_driver)==0){
  uint32_t inst=0;
  if(vqeaf_driver_bind("ahci-sata",(void*)(uintptr_t)abar,&inst)==0)
   vqeaf_cap_set("storage.ahci",VQEAF_CAP_READY,inst,0);
 }
 vqeaf_serial_write("AHCI: 8086:2922 slot=");serial_hex(pci.slot);
 vqeaf_serial_write(" BAR5=");serial_hex(abar);
 vqeaf_serial_write(" disks=");serial_hex(disk_count);
 vqeaf_serial_write(" implemented=");serial_hex(implemented);vqeaf_serial_write("\r\n");
 return 0;
}
int vqeaf_ahci_ready(void){return disk_count?1:0;}
uint32_t vqeaf_ahci_disk_count(void){return disk_count;}
uint32_t vqeaf_ahci_dev_id(uint32_t index){
 return index<disk_count?disks[index].block_id:0xFFFFFFFFu;
}
uint64_t vqeaf_ahci_capacity(uint32_t index){
 return index<disk_count?disks[index].capacity:0;
}
int vqeaf_ahci_read_sync(uint32_t index,uint64_t lba,uint32_t sectors,void *buffer){
 if(index>=disk_count)return -1;
 return transfer(&disks[index],ATA_CMD_READ_DMA_EXT,lba,sectors,
                 (uint64_t)(uintptr_t)buffer);
}
int vqeaf_ahci_write_sync(uint32_t index,uint64_t lba,uint32_t sectors,const void *buffer){
 if(index>=disk_count)return -1;
 return transfer(&disks[index],ATA_CMD_WRITE_DMA_EXT,lba,sectors,
                 (uint64_t)(uintptr_t)buffer);
}
