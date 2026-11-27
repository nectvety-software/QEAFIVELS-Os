#include <assert.h>
#include <errno.h>
#include <string.h>
#include "vqeaf/wlfw_session.h"

static int tx(void *ctx, const vqeaf_qrtr_addr_t *dst,
              const uint8_t *data, size_t len)
{ (void)ctx; (void)dst; (void)data; (void)len; return 0; }
static int rx(void *ctx, uint8_t *buf, size_t cap, size_t *len,
              vqeaf_qrtr_addr_t *src, uint32_t timeout)
{ (void)ctx; (void)buf; (void)cap; (void)len; (void)src; (void)timeout; return -ETIMEDOUT; }
static uint64_t now(void *ctx) { (void)ctx; return 1u; }
int main(void)
{
    vqeaf_wlfw_session_request_t r;
    vqeaf_wlfw_session_result_t result;
    vqeaf_wlfw_client_t client;
    vqeaf_qrtr_io_t io;
    uint8_t bdf[8]={1}, cal[8]={2}, mac[6]={0x02,1,2,3,4,5};
    vqeaf_wlfw_calibration_t cal_entry={.file_id=1,.data=cal,.size=sizeof(cal)};
    memset(&r,0,sizeof(r)); memset(&io,0,sizeof(io));
    io.send=tx; io.recv=rx; io.now_ms=now;
    r.io=&io; r.server.node=1; r.server.port=1;
    r.msa_address=0x93300000; r.msa_size=0x200000;
    r.bdf=bdf; r.bdf_size=sizeof(bdf); r.bdf_file_id=1;
    r.calibration=&cal_entry; r.calibration_count=1;
    r.factory_mac=mac; r.timeout_ms=50; r.retries=1;
    assert(vqeaf_wlfw_session_validate(&r)==0);
    r.factory_mac=NULL; assert(vqeaf_wlfw_session_validate(&r)==-EINVAL);
    r.factory_mac=mac; r.calibration=NULL;
    assert(vqeaf_wlfw_session_validate(&r)==-EINVAL);
    r.calibration=&cal_entry; r.calibration_count=0;
    assert(vqeaf_wlfw_session_validate(&r)==-EINVAL);
    r.calibration_count=1; cal_entry.data=NULL;
    assert(vqeaf_wlfw_session_validate(&r)==-ENODATA);
    cal_entry.data=cal;
    assert(vqeaf_wlfw_session_validate(&r)==0);
    /* A valid request may send QMI only when the caller explicitly runs it;
     * with a transport returning no packets the real state machine times out. */
    assert(vqeaf_wlfw_session_run(&r,&client,&result)==-ETIMEDOUT);
    assert(result.state==VQEAF_WLFW_FAILED && result.fw_ready==0);
    return 0;
}
