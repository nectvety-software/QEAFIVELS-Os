#include "mre_uart.h"
#include "mre_arch.h"
#define GENI_STATUS             0x040u
#define GENI_SER_M_CLK_CFG      0x048u
#define GENI_IF_FIFO_DISABLE_RO 0x064u
#define SE_GENI_M_CMD0          0x600u
#define SE_GENI_M_CMD_CTRL_REG  0x604u
#define SE_GENI_M_IRQ_STATUS    0x610u
#define SE_GENI_M_IRQ_EN        0x614u
#define SE_GENI_M_IRQ_CLEAR     0x618u
#define SE_GENI_TX_FIFOn        0x700u
#define SE_GENI_TX_FIFO_STATUS  0x800u
#define SE_GENI_TX_WATERMARK    0x80Cu
#define SE_UART_TX_TRANS_LEN    0x270u
#define M_GENI_CMD_ACTIVE       (1u << 0)
#define SER_CLK_EN              (1u << 0)
#define FIFO_IF_DISABLE         (1u << 0)
#define M_CMD_DONE_EN           (1u << 0)
#define M_TX_FIFO_WATERMARK_EN  (1u << 30)
#define M_CANCEL_IRQ            (1u << 4)
#define M_ABORT_IRQ             (1u << 5)
#define M_CANCEL_CMD            (1u << 2)
#define M_ABORT_CMD             (1u << 1)
#define TX_FIFO_WC              0x0FFFFFFFu
#define UART_START_TX           (1u << 27)
#define GENI_POLL_DEFAULT       100000u
static uint32_t rd(mre_uart_geni_t *u,uint32_t off)
{ return u->bus.read32(u->bus.context,off); }
static void wr(mre_uart_geni_t *u,uint32_t off,uint32_t value)
{ u->bus.write32(u->bus.context,off,value); }
static int wait_mask(mre_uart_geni_t *u,uint32_t off,uint32_t mask)
{
    uint32_t i;
    for(i=0;i<u->poll_limit;++i) if((rd(u,off)&mask)==mask) return 1;
    return 0;
}
static void recover_command(mre_uart_geni_t *u)
{
    wr(u,SE_GENI_M_IRQ_EN,rd(u,SE_GENI_M_IRQ_EN)&
       ~(M_TX_FIFO_WATERMARK_EN|M_CMD_DONE_EN));
    wr(u,SE_GENI_TX_WATERMARK,0u);
    if((rd(u,GENI_STATUS)&M_GENI_CMD_ACTIVE)==0u) {
        wr(u,SE_GENI_M_IRQ_CLEAR,M_CANCEL_IRQ|M_ABORT_IRQ);
        return;
    }
    wr(u,SE_GENI_M_CMD_CTRL_REG,M_CANCEL_CMD);
    if(wait_mask(u,SE_GENI_M_IRQ_STATUS,M_CANCEL_IRQ)) {
        wr(u,SE_GENI_M_IRQ_CLEAR,M_CANCEL_IRQ|M_ABORT_IRQ);
        return;
    }
    wr(u,SE_GENI_M_CMD_CTRL_REG,M_ABORT_CMD);
    if(wait_mask(u,SE_GENI_M_IRQ_STATUS,M_ABORT_IRQ)) {
        wr(u,SE_GENI_M_IRQ_CLEAR,M_CANCEL_IRQ|M_ABORT_IRQ);
        return;
    }
    wr(u,SE_GENI_M_IRQ_CLEAR,M_CANCEL_IRQ|M_ABORT_IRQ);
}
mre_status_t mre_uart_geni_attach(mre_uart_geni_t *u,const mre_uart_bus_t *bus,
                                  uint32_t depth,uint32_t width,uint32_t polls,
                                  int clocks_ready,int pins_ready,int inherited_verified)
{
    if(u==NULL || bus==NULL || bus->read32==NULL || bus->write32==NULL ||
       depth==0u || depth>256u || width==0u || width>4u) return MRE_E_INVALID;
    u->attached=0;
    if(!clocks_ready || !pins_ready || !inherited_verified) return MRE_E_PERMISSION;
    u->bus=*bus; u->fifo_depth_words=depth; u->fifo_width_bytes=width;
    u->poll_limit=(polls==0u)?GENI_POLL_DEFAULT:polls;
    if((rd(u,GENI_SER_M_CLK_CFG)&SER_CLK_EN)==0u ||
       (rd(u,GENI_IF_FIFO_DISABLE_RO)&FIFO_IF_DISABLE)!=0u)
        return MRE_E_UNSUPPORTED;
    if((rd(u,GENI_STATUS)&M_GENI_CMD_ACTIVE)!=0u) return MRE_E_BUSY;
    u->attached=1; return MRE_OK;
}
mre_status_t mre_uart_geni_write(mre_uart_geni_t *u,const char *data,size_t length)
{
    size_t i=0, expanded=0;
    uint32_t wm=2u, stalls=0u;
    if(u==NULL || data==NULL || !u->attached) return MRE_E_INVALID;
    if(length==0u) return MRE_OK;
    if(length>0x00FFFFFFu) return MRE_E_INVALID;
    for(i=0;i<length;++i) expanded+=(data[i]=='\n')?2u:1u;
    if(expanded>0x00FFFFFFu || u->fifo_depth_words<=wm) return MRE_E_INVALID;
    wr(u,SE_GENI_M_IRQ_CLEAR,M_TX_FIFO_WATERMARK_EN|M_CMD_DONE_EN);
    wr(u,SE_GENI_M_IRQ_EN,rd(u,SE_GENI_M_IRQ_EN)|M_TX_FIFO_WATERMARK_EN|M_CMD_DONE_EN);
    wr(u,SE_GENI_TX_WATERMARK,wm);
    wr(u,SE_UART_TX_TRANS_LEN,(uint32_t)expanded);
    wr(u,SE_GENI_M_CMD0,UART_START_TX);
    mre_arch_dsb_sy();
    i=0;
    while(i<length) {
        size_t consumed=0;
        uint32_t used,available,words=0;
        if(!wait_mask(u,SE_GENI_M_IRQ_STATUS,M_TX_FIFO_WATERMARK_EN)) {
            recover_command(u); u->attached=0; return MRE_E_BUSY;
        }
        used=rd(u,SE_GENI_TX_FIFO_STATUS)&TX_FIFO_WC;
        available=(used>=u->fifo_depth_words)?0u:u->fifo_depth_words-used;
        if(available>wm) available-=wm; else available=0u;
        while(i+consumed<length) {
            char ch=data[i+consumed];
            uint32_t needed=(ch=='\n')?2u:1u;
            if(words+needed>available) break;
            if(ch=='\n') {
                wr(u,SE_GENI_TX_FIFOn,(uint32_t)'\r');
                wr(u,SE_GENI_TX_FIFOn,(uint32_t)'\n');
            } else wr(u,SE_GENI_TX_FIFOn,(uint32_t)(uint8_t)ch);
            words+=needed; ++consumed;
        }
        if(consumed==0u) {
            wr(u,SE_GENI_M_IRQ_CLEAR,M_TX_FIFO_WATERMARK_EN);
            if(++stalls>u->poll_limit) {
                recover_command(u); u->attached=0; return MRE_E_BUSY;
            }
            continue;
        }
        stalls=0u;
        i+=consumed;
        mre_arch_dsb_sy();
        wr(u,SE_GENI_M_IRQ_CLEAR,M_TX_FIFO_WATERMARK_EN);
    }
    if(!wait_mask(u,SE_GENI_M_IRQ_STATUS,M_CMD_DONE_EN)) {
        recover_command(u); u->attached=0; return MRE_E_BUSY;
    }
    wr(u,SE_GENI_M_IRQ_CLEAR,M_CMD_DONE_EN);
    return MRE_OK;
}
