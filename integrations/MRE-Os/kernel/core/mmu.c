#include "mre_mmu.h"
#include <string.h>

#define PT_ENTRIES 512u
#define DESC_VALID 0x1ull
#define DESC_TABLE 0x3ull
#define DESC_ATTRIDX_SHIFT 2u
#define DESC_AP_RO (1ull << 7)
#define DESC_SH_INNER (3ull << 8)
#define DESC_AF (1ull << 10)
#define DESC_PXN (1ull << 53)
#define DESC_UXN (1ull << 54)
#define DESC_L1_ADDR_MASK 0x0000FFFFC0000000ull
#define DESC_L2_ADDR_MASK 0x0000FFFFFFE00000ull
#define DESC_L3_ADDR_MASK 0x0000FFFFFFFFF000ull
#define L1_BLOCK_SIZE (1ull << 30)
#define L2_BLOCK_SIZE (1ull << 21)

#if defined(_MSC_VER)
__declspec(align(4096)) static uint64_t g_tables[MRE_MMU_MAX_TABLES + 1u][PT_ENTRIES];
#else
static uint64_t g_tables[MRE_MMU_MAX_TABLES + 1u][PT_ENTRIES] __attribute__((aligned(4096)));
#endif
static size_t g_tables_used;
static uint64_t g_max_mapped_address;

static uint64_t *alloc_table(void)
{
    uint64_t *table;
    if(g_tables_used>=MRE_MMU_MAX_TABLES) return NULL;
    table=g_tables[++g_tables_used];
    memset(table,0,MRE_PAGE_SIZE);
    return table;
}
static uint64_t make_descriptor(uint64_t pa,const mre_mmu_region_t *r,
                                uint64_t address_mask,int page)
{
    /* Match QEAF's established MAIR contract: AttrIdx0 = normal WB,
     * AttrIdx1 = device-nGnRE. This lets QEAF's existing vm.c add EL0
     * mappings to the MRE-built kernel root without attribute aliasing. */
    uint64_t attr_index = (r->type == MRE_MEMORY_DEVICE) ? 1ull : 0ull;
    uint64_t d=(pa&address_mask)|DESC_VALID|
        (attr_index<<DESC_ATTRIDX_SHIFT)|DESC_SH_INNER|DESC_AF|DESC_PXN|DESC_UXN;
    if(page) d|=2u;
    if(!r->writable) d|=DESC_AP_RO;
    if(r->executable) d&=~(DESC_PXN|DESC_UXN);
    return d;
}
static uint64_t *get_l1(uint32_t i0)
{
    uint64_t d=g_tables[0][i0];
    if((d&3u)==DESC_TABLE) return (uint64_t *)(uintptr_t)(d&DESC_L3_ADDR_MASK);
    if((d&DESC_VALID)!=0u) return NULL;
    {
        uint64_t *t=alloc_table();
        if(t==NULL) return NULL;
        g_tables[0][i0]=((uint64_t)(uintptr_t)t&DESC_L3_ADDR_MASK)|DESC_TABLE;
        return t;
    }
}
static uint64_t *split_l1_block(uint64_t *l1,uint32_t i1)
{
    uint64_t old=l1[i1],base,attrs;
    uint64_t *l2;
    uint32_t i;
    if((old&3u)==DESC_TABLE) return (uint64_t *)(uintptr_t)(old&DESC_L3_ADDR_MASK);
    if((old&DESC_VALID)==0u) return NULL;
    base=old&DESC_L1_ADDR_MASK; attrs=old&~(DESC_L1_ADDR_MASK|3u);
    l2=alloc_table(); if(l2==NULL) return NULL;
    for(i=0;i<PT_ENTRIES;++i)
        l2[i]=((base+(uint64_t)i*L2_BLOCK_SIZE)&DESC_L2_ADDR_MASK)|attrs|DESC_VALID;
    l1[i1]=((uint64_t)(uintptr_t)l2&DESC_L3_ADDR_MASK)|DESC_TABLE;
    return l2;
}
static uint64_t *ensure_l2(uint64_t *l1,uint32_t i1)
{
    uint64_t d=l1[i1];
    if((d&3u)==DESC_TABLE) return (uint64_t *)(uintptr_t)(d&DESC_L3_ADDR_MASK);
    if((d&DESC_VALID)!=0u) return split_l1_block(l1,i1);
    {
        uint64_t *t=alloc_table(); if(t==NULL) return NULL;
        l1[i1]=((uint64_t)(uintptr_t)t&DESC_L3_ADDR_MASK)|DESC_TABLE; return t;
    }
}
static uint64_t *split_l2_block(uint64_t *l2,uint32_t i2)
{
    uint64_t old=l2[i2],base,attrs;
    uint64_t *l3;
    uint32_t i;
    if((old&3u)==DESC_TABLE) return (uint64_t *)(uintptr_t)(old&DESC_L3_ADDR_MASK);
    if((old&DESC_VALID)==0u) return NULL;
    base=old&DESC_L2_ADDR_MASK; attrs=old&~(DESC_L2_ADDR_MASK|3u);
    l3=alloc_table(); if(l3==NULL) return NULL;
    for(i=0;i<PT_ENTRIES;++i)
        l3[i]=((base+(uint64_t)i*MRE_PAGE_SIZE)&DESC_L3_ADDR_MASK)|attrs|DESC_TABLE;
    l2[i2]=((uint64_t)(uintptr_t)l3&DESC_L3_ADDR_MASK)|DESC_TABLE;
    return l3;
}
static uint64_t *ensure_l3(uint64_t *l2,uint32_t i2)
{
    uint64_t d=l2[i2];
    if((d&3u)==DESC_TABLE) return (uint64_t *)(uintptr_t)(d&DESC_L3_ADDR_MASK);
    if((d&DESC_VALID)!=0u) return split_l2_block(l2,i2);
    {
        uint64_t *t=alloc_table(); if(t==NULL) return NULL;
        l2[i2]=((uint64_t)(uintptr_t)t&DESC_L3_ADDR_MASK)|DESC_TABLE; return t;
    }
}
static mre_status_t map_page(uint64_t va,uint64_t pa,const mre_mmu_region_t *r)
{
    uint32_t i0=(uint32_t)((va>>39)&511u),i1=(uint32_t)((va>>30)&511u);
    uint32_t i2=(uint32_t)((va>>21)&511u),i3=(uint32_t)((va>>12)&511u);
    uint64_t *l1=get_l1(i0),*l2,*l3,d;
    if(l1==NULL) return MRE_E_NOMEM;
    l2=ensure_l2(l1,i1); if(l2==NULL) return MRE_E_NOMEM;
    l3=ensure_l3(l2,i2); if(l3==NULL) return MRE_E_NOMEM;
    d=make_descriptor(pa,r,DESC_L3_ADDR_MASK,1);
    if((l3[i3]&DESC_VALID)!=0u && (l3[i3]&DESC_L3_ADDR_MASK)!=(d&DESC_L3_ADDR_MASK))
        return MRE_E_EXISTS;
    l3[i3]=d; return MRE_OK;
}
static mre_status_t map_l2_block(uint64_t va,uint64_t pa,const mre_mmu_region_t *r)
{
    uint32_t i0=(uint32_t)((va>>39)&511u),i1=(uint32_t)((va>>30)&511u);
    uint32_t i2=(uint32_t)((va>>21)&511u),i;
    uint64_t *l1=get_l1(i0),*l2,d,old;
    if(l1==NULL) return MRE_E_NOMEM;
    l2=ensure_l2(l1,i1); if(l2==NULL) return MRE_E_NOMEM;
    old=l2[i2];
    if((old&3u)==DESC_TABLE) {
        for(i=0;i<PT_ENTRIES;++i) {
            mre_status_t st=map_page(va+(uint64_t)i*MRE_PAGE_SIZE,
                                     pa+(uint64_t)i*MRE_PAGE_SIZE,r);
            if(st!=MRE_OK) return st;
        }
        return MRE_OK;
    }
    d=make_descriptor(pa,r,DESC_L2_ADDR_MASK,0);
    if((old&DESC_VALID)!=0u && (old&DESC_L2_ADDR_MASK)!=(d&DESC_L2_ADDR_MASK))
        return MRE_E_EXISTS;
    l2[i2]=d; return MRE_OK;
}
static mre_status_t map_l1_block(uint64_t va,uint64_t pa,const mre_mmu_region_t *r)
{
    uint32_t i0=(uint32_t)((va>>39)&511u),i1=(uint32_t)((va>>30)&511u),i;
    uint64_t *l1=get_l1(i0),old,d;
    if(l1==NULL) return MRE_E_NOMEM;
    old=l1[i1];
    if((old&3u)==DESC_TABLE) {
        for(i=0;i<PT_ENTRIES;++i) {
            mre_status_t st=map_l2_block(va+(uint64_t)i*L2_BLOCK_SIZE,
                                         pa+(uint64_t)i*L2_BLOCK_SIZE,r);
            if(st!=MRE_OK) return st;
        }
        return MRE_OK;
    }
    d=make_descriptor(pa,r,DESC_L1_ADDR_MASK,0);
    if((old&DESC_VALID)!=0u && (old&DESC_L1_ADDR_MASK)!=(d&DESC_L1_ADDR_MASK))
        return MRE_E_EXISTS;
    l1[i1]=d; return MRE_OK;
}
mre_status_t mre_mmu_reset(void)
{
    memset(g_tables,0,sizeof(g_tables)); g_tables_used=0; g_max_mapped_address=0u; return MRE_OK;
}
mre_status_t mre_mmu_map_range(const mre_mmu_region_t *r)
{
    uint64_t va,pa,end;
    if(r==NULL || r->size==0u || r->type<MRE_MEMORY_DEVICE ||
       r->type>MRE_MEMORY_NORMAL_WB || (r->writable&&r->executable) ||
       (r->virtual_address&(MRE_PAGE_SIZE-1u))!=0u ||
       (r->physical_address&(MRE_PAGE_SIZE-1u))!=0u ||
       (r->size&(MRE_PAGE_SIZE-1u))!=0u ||
       r->virtual_address>UINT64_MAX-r->size || r->physical_address>UINT64_MAX-r->size ||
       r->virtual_address+r->size>(1ull<<48) ||
       r->physical_address+r->size>(1ull<<48)) return MRE_E_INVALID;
    end=r->virtual_address+r->size;
    for(va=r->virtual_address,pa=r->physical_address;va<end;) {
        uint64_t remaining=end-va;
        mre_status_t st;
        if((va&(L1_BLOCK_SIZE-1u))==0u && (pa&(L1_BLOCK_SIZE-1u))==0u &&
           remaining>=L1_BLOCK_SIZE) {
            st=map_l1_block(va,pa,r); if(st!=MRE_OK) return st;
            va+=L1_BLOCK_SIZE; pa+=L1_BLOCK_SIZE;
        } else if((va&(L2_BLOCK_SIZE-1u))==0u && (pa&(L2_BLOCK_SIZE-1u))==0u &&
                  remaining>=L2_BLOCK_SIZE) {
            st=map_l2_block(va,pa,r); if(st!=MRE_OK) return st;
            va+=L2_BLOCK_SIZE; pa+=L2_BLOCK_SIZE;
        } else {
            st=map_page(va,pa,r); if(st!=MRE_OK) return st;
            va+=MRE_PAGE_SIZE; pa+=MRE_PAGE_SIZE;
        }
    }
    if(pa>g_max_mapped_address) g_max_mapped_address=pa;
    return MRE_OK;
}
const uint64_t *mre_mmu_root_table(void) { return g_tables[0]; }
size_t mre_mmu_tables_used(void) { return g_tables_used; }
uint64_t mre_mmu_max_mapped_address(void) { return g_max_mapped_address; }
