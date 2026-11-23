#include "mre_fdt.h"
#include "mre_violet.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv)
{
    FILE *fp;
    long length;
    void *blob;
    mre_fdt_t fdt;
    mre_violet_boot_info_t info;
    assert(argc==2);
    fp=fopen(argv[1],"rb");assert(fp!=NULL);
    assert(fseek(fp,0,SEEK_END)==0);length=ftell(fp);assert(length>0);
    assert(fseek(fp,0,SEEK_SET)==0);
    blob=malloc((size_t)length);assert(blob!=NULL);
    assert(fread(blob,1,(size_t)length,fp)==(size_t)length);fclose(fp);
    assert(mre_fdt_open(&fdt,blob,(size_t)length)==MRE_OK);
    assert(mre_violet_discover(&fdt,&info)==MRE_OK);
    assert(info.board_identity_verified==0u);
    assert(info.gic_found && info.gic.version==MRE_GIC_V3);
    assert(info.gic.distributor_base==0x17A00000ull);
    assert(info.gic.redistributor_base==0x17A60000ull);
    assert(info.uart_found && info.uart_base==0x00880000ull);
    assert(info.uart_irq==633u);
    assert(info.timer_found && info.timer_irq==18u);
    assert(info.memory_map_valid==0u);
    assert(mre_violet_mmu_prerequisites(&info)==MRE_E_UNSUPPORTED);
    printf("SM6150 reference DTB: PASS (board_identity=%u, size=%llu, reserved=%u, dynamic=%u; MMU gated)\n",
           info.board_identity_verified,(unsigned long long)info.dtb_size,
           info.reserved_region_count,info.dynamic_reserved_count);
    free(blob);return 0;
}
