#include "../include/vqeaf/pci.h"
static inline void outl(uint16_t port,uint32_t value){__asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port));}
static inline uint32_t inl(uint16_t port){uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(port));return v;}
static uint32_t config_addr(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset){
 return 0x80000000u|((uint32_t)bus<<16)|((uint32_t)slot<<11)|((uint32_t)func<<8)|(uint32_t)(offset&0xFCu);
}
uint32_t vqeaf_pci_read32(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset){
 outl(0xCF8u,config_addr(bus,slot,func,offset));
 return inl(0xCFCu);
}
void vqeaf_pci_write32(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset,uint32_t value){
 outl(0xCF8u,config_addr(bus,slot,func,offset));
 outl(0xCFCu,value);
}
uint16_t vqeaf_pci_read16(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset){
 uint32_t v=vqeaf_pci_read32(bus,slot,func,offset);
 return (uint16_t)(v>>((offset&2u)*8u));
}
void vqeaf_pci_write16(uint8_t bus,uint8_t slot,uint8_t func,uint8_t offset,uint16_t value){
 uint32_t v=vqeaf_pci_read32(bus,slot,func,offset);
 unsigned shift=(offset&2u)*8u;
 v=(v&~(0xFFFFu<<shift))|((uint32_t)value<<shift);
 vqeaf_pci_write32(bus,slot,func,offset,v);
}
int vqeaf_pci_find(uint16_t vendor,uint16_t device,VqeafPciDevice *out){
 if(!out)return -1;
 for(uint8_t bus=0;bus<2;bus++)
  for(uint8_t slot=0;slot<32;slot++)
   for(uint8_t func=0;func<8;func++){
    uint32_t id=vqeaf_pci_read32(bus,slot,func,0x00u);
    uint16_t v=(uint16_t)(id&0xFFFFu),d=(uint16_t)(id>>16);
    if(v==0xFFFFu)continue;
    if(v!=vendor||d!=device)continue;
    out->bus=bus;out->slot=slot;out->func=func;
    out->vendor=v;out->device=d;
    for(unsigned i=0;i<6;i++)out->bars[i]=vqeaf_pci_read32(bus,slot,func,(uint8_t)(0x10u+i*4u));
    out->bar0=out->bars[0];
    out->irq=(uint8_t)(vqeaf_pci_read32(bus,slot,func,0x3Cu)&0xFFu);
    return 0;
   }
 return -2;
}
void vqeaf_pci_enable_busmaster(const VqeafPciDevice *pci){
 if(!pci)return;
 uint16_t cmd=vqeaf_pci_read16(pci->bus,pci->slot,pci->func,0x04u);
 cmd=(uint16_t)((cmd|0x0007u)&~0x0400u);
 vqeaf_pci_write16(pci->bus,pci->slot,pci->func,0x04u,cmd);
}
