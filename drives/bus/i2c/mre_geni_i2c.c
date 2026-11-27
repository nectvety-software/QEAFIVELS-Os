#include "mre_geni_i2c.h"
#define R_STATUS 0x040u
#define R_IF_DISABLE 0x064u
#define R_M_CMD0 0x600u
#define R_M_IRQ_STATUS 0x610u
#define R_M_IRQ_EN 0x614u
#define R_M_IRQ_CLEAR 0x618u
#define R_M_CMD_CTRL 0x624u
#define R_TX_LEN 0x26cu
#define R_RX_LEN 0x270u
#define R_TX_FIFO 0x700u
#define R_RX_FIFO 0x780u
#define R_TX_FIFO_STATUS 0x800u
#define R_RX_FIFO_STATUS 0x804u
#define R_TX_WM 0x80cu
#define FIFO_DISABLED (1u<<0)
#define CMD_ACTIVE (1u<<0)
#define IRQ_DONE (1u<<0)
#define IRQ_ERRORS ((1u<<1)|(1u<<2)|(1u<<3)|(1u<<5)|(1u<<10)|(1u<<12)|(1u<<13))
#define IRQ_TX_WM (1u<<30)
#define IRQ_RX_WM (1u<<26)
#define IRQ_RX_LAST (1u<<27)
#define OP_WRITE 1u
#define OP_READ 2u
#define OP_WRITE_READ 3u
#define ADDR_SHIFT 9u
#define READ_ACK (1u<<7)
#define CMD_ABORT (1u<<1)
#define MAX_XFER 256u
static uint32_t rd(mre_geni_i2c_bus_t*b,uint32_t o){return b->read32(b->context,o);}
static void wr(mre_geni_i2c_bus_t*b,uint32_t o,uint32_t v){b->write32(b->context,o,v);}
static mre_status_t wait_irq(mre_geni_i2c_bus_t*b,uint32_t mask){
 uint32_t i,s;for(i=0;i<b->poll_limit;i++){s=rd(b,R_M_IRQ_STATUS);if(s&IRQ_ERRORS){wr(b,R_M_IRQ_CLEAR,s);return (s&(1u<<10))?MRE_E_NOT_FOUND:MRE_E_IO;}if(s&mask)return MRE_OK;}
 wr(b,R_M_CMD_CTRL,CMD_ABORT);return MRE_E_BUSY;
}
static mre_status_t tx_fifo(mre_geni_i2c_bus_t*b,const uint8_t*p,size_t n){
 size_t pos=0;uint32_t v;while(pos<n){mre_status_t st=wait_irq(b,IRQ_TX_WM);if(st!=MRE_OK)return st;
  while(pos<n){size_t j,chunk=n-pos;if(chunk>4u)chunk=4u;v=0;for(j=0;j<chunk;j++)v|=(uint32_t)p[pos+j]<<(8u*(uint32_t)j);wr(b,R_TX_FIFO,v);pos+=chunk;
   if((rd(b,R_TX_FIFO_STATUS)&0x0fffffffu)>=16u)break;}
  wr(b,R_M_IRQ_CLEAR,IRQ_TX_WM);
 }return MRE_OK;
}
static mre_status_t rx_fifo(mre_geni_i2c_bus_t*b,uint8_t*p,size_t n){
 size_t pos=0;uint32_t s,words,v;while(pos<n){mre_status_t st=wait_irq(b,IRQ_RX_WM|IRQ_RX_LAST);if(st!=MRE_OK)return st;
  s=rd(b,R_M_IRQ_STATUS);words=rd(b,R_RX_FIFO_STATUS)&0x01ffffffu;
  while(words--&&pos<n){unsigned j;v=rd(b,R_RX_FIFO);for(j=0;j<4u&&pos<n;j++,pos++){p[pos]=(uint8_t)v;v>>=8;}}
  wr(b,R_M_IRQ_CLEAR,s&(IRQ_RX_WM|IRQ_RX_LAST));
 }return MRE_OK;
}
static mre_status_t xfer(mre_geni_i2c_bus_t*b,uint8_t a,uint32_t op,const uint8_t*t,size_t tn,uint8_t*r,size_t rn){
 mre_status_t st;uint32_t param;if(!b||!b->attached||a>0x7fu||tn>MAX_XFER||rn>MAX_XFER||(tn&&!t)||(rn&&!r)||(!tn&&!rn))return MRE_E_INVALID;
 if(rd(b,R_STATUS)&CMD_ACTIVE)return MRE_E_BUSY;
 wr(b,R_M_IRQ_CLEAR,0xffffffffu);wr(b,R_M_IRQ_EN,IRQ_DONE|IRQ_ERRORS|IRQ_TX_WM|IRQ_RX_WM|IRQ_RX_LAST);
 if(tn) wr(b,R_TX_LEN,(uint32_t)tn);
 if(rn) wr(b,R_RX_LEN,(uint32_t)rn);
 wr(b,R_TX_WM,1u);
 param=((uint32_t)a<<ADDR_SHIFT)|(rn?READ_ACK:0u);wr(b,R_M_CMD0,(op<<27)|param);
 if(tn){st=tx_fifo(b,t,tn);if(st!=MRE_OK)goto fail;}if(rn){st=rx_fifo(b,r,rn);if(st!=MRE_OK)goto fail;}
 st=wait_irq(b,IRQ_DONE);wr(b,R_M_IRQ_CLEAR,rd(b,R_M_IRQ_STATUS));return st;
fail:wr(b,R_M_CMD_CTRL,CMD_ABORT);wr(b,R_M_IRQ_CLEAR,rd(b,R_M_IRQ_STATUS));return st;
}
mre_status_t mre_geni_i2c_attach(mre_geni_i2c_bus_t*b){
 if(!b||!b->read32||!b->write32) return MRE_E_INVALID;
 b->attached=0;
 if(!b->resources_verified||!b->fifo_mode_verified) return MRE_E_PERMISSION;
 if(rd(b,R_IF_DISABLE)&FIFO_DISABLED) return MRE_E_UNSUPPORTED;
 if(rd(b,R_STATUS)&CMD_ACTIVE) return MRE_E_BUSY;
 if(!b->poll_limit) b->poll_limit=100000u;
 b->attached=1;
 return MRE_OK;
}
mre_status_t mre_geni_i2c_write(mre_geni_i2c_bus_t*b,uint8_t a,const uint8_t*p,size_t n){return xfer(b,a,OP_WRITE,p,n,0,0);}
mre_status_t mre_geni_i2c_read(mre_geni_i2c_bus_t*b,uint8_t a,uint8_t*p,size_t n){return xfer(b,a,OP_READ,0,0,p,n);}
mre_status_t mre_geni_i2c_write_read(mre_geni_i2c_bus_t*b,uint8_t a,const uint8_t*t,size_t tn,uint8_t*r,size_t rn){return xfer(b,a,OP_WRITE_READ,t,tn,r,rn);}
