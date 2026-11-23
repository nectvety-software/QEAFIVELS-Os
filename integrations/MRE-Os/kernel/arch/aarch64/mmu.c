#include "mre_mmu.h"
#if defined(__aarch64__)
mre_status_t mre_mmu_enable(void)
{
    /* QEAF shared descriptor convention: Attr0 normal WB, Attr1 device-nGnRE. */
    uint64_t mair=0x00000000000004FFull;
    uint64_t mmfr0,sctlr,tcr,ttbr,parange,pa_bits,ips;
    const uint64_t *root=mre_mmu_root_table();
    if(root==NULL || mre_mmu_tables_used()==0u) return MRE_E_INVALID;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    if((sctlr&((1ull<<0)|(1ull<<2)))!=0u) return MRE_E_BUSY;
    __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
    parange=mmfr0&0xFu;
    if(parange>5u) ips=5u; else ips=parange;
    switch(parange) {
        case 0u: pa_bits=32u; break;
        case 1u: pa_bits=36u; break;
        case 2u: pa_bits=40u; break;
        case 3u: pa_bits=42u; break;
        case 4u: pa_bits=44u; break;
        default: pa_bits=48u; break;
    }
    if(mre_mmu_max_mapped_address()>(1ull<<pa_bits)) return MRE_E_UNSUPPORTED;
    ttbr=(uint64_t)(uintptr_t)root;
    if((ttbr&0xFFFu)!=0u || ttbr>=(1ull<<pa_bits)) return MRE_E_INVALID;
    tcr=16ull|(1ull<<8)|(1ull<<10)|(3ull<<12)|(16ull<<16)|
        (1ull<<24)|(1ull<<26)|(3ull<<28)|(2ull<<30)|(ips<<32);
    __asm__ volatile(
        "dsb sy\n"
        "msr mair_el1, %0\n"
        "msr tcr_el1, %1\n"
        "msr ttbr0_el1, %2\n"
        "dsb ish\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb"
        : : "r"(mair),"r"(tcr),"r"(ttbr) : "memory");
    sctlr|=(1ull<<0)|(1ull<<2)|(1ull<<12)|(1ull<<19);
    __asm__ volatile("msr sctlr_el1, %0\nisb" : : "r"(sctlr) : "memory");
    return MRE_OK;
}
#else
mre_status_t mre_mmu_enable(void) { return MRE_E_UNSUPPORTED; }
#endif
