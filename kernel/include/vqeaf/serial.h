#ifndef VQEAF_SERIAL_H
#define VQEAF_SERIAL_H
#include <stdint.h>
static inline void vqeaf_out8(uint16_t port,uint8_t value){__asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));}
static inline uint8_t vqeaf_in8(uint16_t port){uint8_t value;__asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));return value;}
static inline void vqeaf_serial_init(void){
 vqeaf_out8(0x3F9,0x00);vqeaf_out8(0x3FB,0x80);vqeaf_out8(0x3F8,0x01);
 vqeaf_out8(0x3F9,0x00);vqeaf_out8(0x3FB,0x03);vqeaf_out8(0x3FA,0xC7);vqeaf_out8(0x3FC,0x0B);
}
static inline void vqeaf_serial_char(char c){
 for(unsigned i=0;i<1000000;i++)if(vqeaf_in8(0x3FD)&0x20)break;
 vqeaf_out8(0x3F8,(uint8_t)c);
}
static inline void vqeaf_serial_write(const char *s){while(*s)vqeaf_serial_char(*s++);}
#endif
