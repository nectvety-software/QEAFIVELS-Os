#include "mre_violet.h"
#include "mre_arch.h"
#include "mre_mmu.h"
#include <string.h>

static mre_violet_boot_info_t g_info;
static mre_kernel_boot_report_t g_boot_report;

static uint32_t fdt_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static mre_status_t find_one(const mre_fdt_t *fdt,const char *compat,mre_fdt_node_t *node)
{
    uint32_t cursor=0;
    return mre_fdt_find_compatible(fdt,compat,&cursor,node);
}
static int root_has_compatible(const mre_fdt_t *fdt,const char *wanted)
{
    mre_fdt_node_t root;uint32_t cursor=0,len;const void *value;
    const char *p;size_t wanted_len=strlen(wanted);
    if(mre_fdt_find_node_name(fdt,"",&cursor,&root)!=MRE_OK ||
       mre_fdt_get_property(fdt,root,"compatible",&value,&len)!=MRE_OK) return 0;
    p=(const char *)value;
    {
        uint32_t offset=0;
        while(offset<len) {
            size_t n=0;
            while(offset+n<len && p[offset+n]!=0) ++n;
            if(offset+n>=len) return 0;
            if(n==wanted_len && memcmp(p+offset,wanted,n)==0) return 1;
            offset+=(uint32_t)n+1u;
        }
    }
    return 0;
}
static mre_status_t get_reg_or_fail(const mre_fdt_t *fdt,mre_fdt_node_t node,
                                    uint32_t index,uint64_t *base,uint64_t *size)
{
    mre_fdt_reg_t reg;
    mre_status_t st=mre_fdt_get_reg(fdt,node,index,&reg);
    if(st!=MRE_OK || reg.size==0u) return MRE_E_INVALID;
    *base=reg.address; *size=reg.size; return MRE_OK;
}
static void copy_supply_name(char *dst,const char *src)
{
    size_t i=0u;
    while(i+1u<MRE_VIOLET_DISPLAY_SUPPLY_NAME && src[i]!=0) {
        dst[i]=src[i]; ++i;
    }
    dst[i]=0;
}
static mre_status_t add_supply(const mre_fdt_t *fdt,mre_fdt_node_t node,
                               const char *name,mre_violet_display_info_t *display)
{
    const void *data; uint32_t length; uint32_t phandle; mre_fdt_node_t provider;
    mre_violet_supply_ref_t *supply;
    if(mre_fdt_get_property(fdt,node,name,&data,&length)!=MRE_OK) return MRE_OK;
    if(length!=4u) return MRE_E_INVALID;
    phandle=fdt_be32((const uint8_t *)data);
    if(phandle==0u || mre_fdt_find_phandle(fdt,phandle,&provider)!=MRE_OK)
        return MRE_E_INVALID;
    if(display->supply_count>=MRE_VIOLET_MAX_DISPLAY_SUPPLIES) return MRE_E_NOMEM;
    supply=&display->supplies[display->supply_count++];
    supply->phandle=phandle; copy_supply_name(supply->name,name);
    return MRE_OK;
}
static mre_status_t parse_clock_refs(const mre_fdt_t *fdt,mre_fdt_node_t node,
                                     const char *name,mre_violet_clock_ref_t *out,
                                     uint32_t *count)
{
    const void *data; uint32_t length,i;
    mre_status_t st=mre_fdt_get_property(fdt,node,name,&data,&length);
    if(st!=MRE_OK) return st;
    if(length==0u || (length&7u)!=0u || length/8u>MRE_VIOLET_MAX_DISPLAY_CLOCKS)
        return MRE_E_INVALID;
    *count=length/8u;
    for(i=0u;i<*count;++i) {
        const uint8_t *p=(const uint8_t *)data+i*8u;
        mre_fdt_node_t provider;
        out[i].provider_phandle=fdt_be32(p);
        out[i].id=fdt_be32(p+4u);
        if(out[i].provider_phandle==0u ||
           mre_fdt_find_phandle(fdt,out[i].provider_phandle,&provider)!=MRE_OK)
            return MRE_E_INVALID;
    }
    return MRE_OK;
}
static mre_status_t parse_interrupt_extended(const mre_fdt_t *fdt,
                                              mre_fdt_node_t node,
                                              uint32_t *controller,
                                              uint32_t *index)
{
    const void *data; uint32_t length,cells; uint32_t phandle;
    mre_fdt_node_t provider;
    mre_status_t st=mre_fdt_get_property(fdt,node,"interrupts-extended",&data,&length);
    if(st!=MRE_OK) return st;
    if(length<8u) return MRE_E_INVALID;
    phandle=fdt_be32((const uint8_t *)data);
    if(phandle==0u || mre_fdt_find_phandle(fdt,phandle,&provider)!=MRE_OK)
        return MRE_E_INVALID;
    st=mre_fdt_get_u32(fdt,provider,"#interrupt-cells",&cells);
    if(st!=MRE_OK || cells==0u || cells>4u || length<(cells+1u)*4u)
        return MRE_E_INVALID;
    *controller=phandle;
    *index=fdt_be32((const uint8_t *)data+4u);
    return MRE_OK;
}
static mre_status_t parse_reset_gpio(const mre_fdt_t *fdt,mre_fdt_node_t node,
                                     mre_violet_display_info_t *display)
{
    const void *data; uint32_t length,cells,phandle; mre_fdt_node_t provider;
    mre_status_t st=mre_fdt_get_property(fdt,node,"reset-gpios",&data,&length);
    if(st!=MRE_OK) return st;
    if(length<8u) return MRE_E_INVALID;
    phandle=fdt_be32((const uint8_t *)data);
    if(phandle==0u || mre_fdt_find_phandle(fdt,phandle,&provider)!=MRE_OK)
        return MRE_E_INVALID;
    st=mre_fdt_get_u32(fdt,provider,"#gpio-cells",&cells);
    if(st!=MRE_OK || cells==0u || cells>2u || length!=(cells+1u)*4u)
        return MRE_E_INVALID;
    display->reset_phandle=phandle;
    display->reset_gpio=fdt_be32((const uint8_t *)data+4u);
    display->reset_flags=(cells==2u)?fdt_be32((const uint8_t *)data+8u):0u;
    display->reset_active_low=(display->reset_flags&1u)?1u:0u;
    return MRE_OK;
}
static mre_status_t parse_lanes(const mre_fdt_t *fdt,mre_fdt_node_t dsi,
                                mre_violet_display_info_t *display)
{
    const void *data; uint32_t length,i,j,count; uint32_t cursor=0u;
    mre_fdt_node_t endpoint;
    mre_status_t st=mre_fdt_find_descendant_property(fdt,dsi,"data-lanes",
                                                      &cursor,&endpoint);
    if(st!=MRE_OK) return st;
    st=mre_fdt_get_property(fdt,endpoint,"data-lanes",&data,&length);
    if(st!=MRE_OK) return st;
    if(length==0u || (length&3u)!=0u) return MRE_E_INVALID;
    count=length/4u;
    if(count>4u) return MRE_E_INVALID;
    display->lane_count=count;
    for(i=0u;i<count;++i) {
        display->lanes[i]=(uint8_t)fdt_be32((const uint8_t *)data+i*4u);
        if(display->lanes[i]>3u) return MRE_E_INVALID;
        for(j=0u;j<i;++j) if(display->lanes[j]==display->lanes[i])
            return MRE_E_INVALID;
    }
    return MRE_OK;
}
static mre_status_t discover_display(const mre_fdt_t *fdt,
                                     mre_violet_boot_info_t *info)
{
    mre_violet_display_info_t *display=&info->display;
    mre_fdt_node_t node;
    mre_status_t st;
    uint32_t any=0u,complete=1u;
    memset(display,0,sizeof(*display));

    st=find_one(fdt,"qcom,sm6150-mdss",&node);
    if(st==MRE_OK) {
        any=1u; display->mdss_found=1u;
        if(get_reg_or_fail(fdt,node,0u,&display->mdss_base,&display->mdss_size)!=MRE_OK)
            return MRE_E_INVALID;
        {
            const void *ints; uint32_t length,cells=3u;
            if(mre_fdt_get_interrupt_cells(fdt,node,&cells)!=MRE_OK) cells=3u;
            if(mre_fdt_get_property(fdt,node,"interrupts",&ints,&length)==MRE_OK &&
               cells>=3u && length>=cells*4u) {
                const uint8_t *p=(const uint8_t *)ints;
                if(fdt_be32(p)==0u) display->mdss_irq=32u+fdt_be32(p+4u);
            }
        }
    } else complete=0u;

    st=find_one(fdt,"qcom,sm6150-dpu",&node);
    if(st==MRE_OK) {
        any=1u; display->dpu_found=1u;
        if(get_reg_or_fail(fdt,node,0u,&display->dpu_base,&display->dpu_size)!=MRE_OK ||
           get_reg_or_fail(fdt,node,1u,&display->dpu_vbif_base,&display->dpu_vbif_size)!=MRE_OK)
            return MRE_E_INVALID;
        st=parse_interrupt_extended(fdt,node,&display->dpu_irq_controller,
                                    &display->dpu_irq_index);
        if(st!=MRE_OK && st!=MRE_E_NOT_FOUND) return st;
    } else complete=0u;

    st=find_one(fdt,"qcom,sm6150-dsi-ctrl",&node);
    if(st==MRE_OK) {
        any=1u; display->dsi_found=1u;
        if(get_reg_or_fail(fdt,node,0u,&display->dsi_base,&display->dsi_size)!=MRE_OK)
            return MRE_E_INVALID;
        st=parse_interrupt_extended(fdt,node,&display->dsi_irq_controller,
                                    &display->dsi_irq_index);
        if(st!=MRE_OK) return st;
        st=parse_clock_refs(fdt,node,"clocks",display->dsi_clocks,
                            &display->dsi_clock_count);
        if(st!=MRE_OK) return st;
        st=parse_clock_refs(fdt,node,"assigned-clocks",display->dsi_assigned_clocks,
                            &display->dsi_assigned_clock_count);
        if(st!=MRE_OK && st!=MRE_E_NOT_FOUND) return st;
        st=parse_clock_refs(fdt,node,"assigned-clock-parents",
                            display->dsi_assigned_parents,
                            &display->dsi_assigned_parent_count);
        if(st!=MRE_OK && st!=MRE_E_NOT_FOUND) return st;
        st=parse_lanes(fdt,node,display);
        if(st!=MRE_OK) return st;
        st=add_supply(fdt,node,"vdda-supply",display);
        if(st!=MRE_OK) return st;
    } else complete=0u;

    st=find_one(fdt,"qcom,sm6150-dsi-phy-14nm",&node);
    if(st==MRE_OK) {
        any=1u; display->phy_found=1u;
        if(get_reg_or_fail(fdt,node,0u,&display->phy_base,&display->phy_size)!=MRE_OK ||
           get_reg_or_fail(fdt,node,1u,&display->phy_lane_base,&display->phy_lane_size)!=MRE_OK ||
           get_reg_or_fail(fdt,node,2u,&display->phy_pll_base,&display->phy_pll_size)!=MRE_OK)
            return MRE_E_INVALID;
        st=add_supply(fdt,node,"vcca-supply",display);
        if(st!=MRE_OK) return st;
    } else complete=0u;

    st=find_one(fdt,"qcom,qcs615-dispcc",&node);
    if(st==MRE_OK) {
        any=1u; display->dispcc_found=1u;
        if(get_reg_or_fail(fdt,node,0u,&display->dispcc_base,&display->dispcc_size)!=MRE_OK)
            return MRE_E_INVALID;
    } else complete=0u;

    st=find_one(fdt,"novatek,nt36672a-shenchao",&node);
    if(st==MRE_OK) {
        const void *data; uint32_t length; uint32_t phandle; mre_fdt_node_t provider;
        any=1u; display->panel_found=1u;
        st=add_supply(fdt,node,"vddi-supply",display); if(st!=MRE_OK) return st;
        st=add_supply(fdt,node,"avdd-supply",display); if(st!=MRE_OK) return st;
        st=add_supply(fdt,node,"avee-supply",display); if(st!=MRE_OK) return st;
        st=parse_reset_gpio(fdt,node,display); if(st!=MRE_OK) return st;
        st=mre_fdt_get_property(fdt,node,"backlight",&data,&length);
        if(st==MRE_OK) {
            if(length!=4u) return MRE_E_INVALID;
            phandle=fdt_be32((const uint8_t *)data);
            if(phandle==0u || mre_fdt_find_phandle(fdt,phandle,&provider)!=MRE_OK)
                return MRE_E_INVALID;
            display->backlight_phandle=phandle;
        } else if(st!=MRE_E_NOT_FOUND) return st;
        display->panel_timing_present=
            (mre_fdt_get_property(fdt,node,"panel-timing",&data,&length)==MRE_OK)?1u:0u;
        display->panel_command_data_present=
            (mre_fdt_get_property(fdt,node,"qcom,mdss-dsi-on-command",
                                  &data,&length)==MRE_OK)?1u:0u;
    } else complete=0u;

    display->found=any;
    return (any && complete)?MRE_OK:MRE_E_NOT_FOUND;
}
static mre_status_t discover_memory(const mre_fdt_t *fdt,mre_violet_boot_info_t *info)
{
    mre_fdt_node_t node;
    uint32_t cursor=0;
    size_t reservation_count=0, i;
    mre_fdt_reg_t reservations[MRE_VIOLET_MAX_RESERVED_REGIONS];
    mre_status_t reservation_status=mre_fdt_get_reservations(
        fdt,reservations,MRE_VIOLET_MAX_RESERVED_REGIONS,&reservation_count);
    if(reservation_status==MRE_E_NOMEM) ++info->dynamic_reserved_count;
    else if(reservation_status!=MRE_OK) return reservation_status;
    for(i=0;i<reservation_count;++i) {
        mre_violet_region_t *dst=&info->reserved[info->reserved_region_count++];
        dst->base=reservations[i].address;dst->size=reservations[i].size;
    }
    while(info->memory_region_count<MRE_VIOLET_MAX_MEMORY_REGIONS &&
          mre_fdt_find_property_string(fdt,"device_type","memory",&cursor,&node)==MRE_OK) {
        uint32_t index;
        for(index=0;index<8u;++index) {
            mre_fdt_reg_t reg;
            mre_status_t st=mre_fdt_get_reg(fdt,node,index,&reg);
            if(st!=MRE_OK) break;
            if(reg.size==0u) continue;
            info->memory[info->memory_region_count].base=reg.address;
            info->memory[info->memory_region_count].size=reg.size;
            ++info->memory_region_count;
        }
    }
    {
        uint32_t parent_cursor=0, child_cursor=0;
        mre_fdt_node_t reserved_parent, child;
        if(mre_fdt_find_node_name(fdt,"reserved-memory",&parent_cursor,&reserved_parent)==MRE_OK) {
            while(mre_fdt_find_child(fdt,reserved_parent,&child_cursor,&child)==MRE_OK) {
                mre_fdt_reg_t reg;
                mre_status_t st=mre_fdt_get_reg(fdt,child,0u,&reg);
                if(st==MRE_OK && reg.size!=0u) {
                    if(info->reserved_region_count<MRE_VIOLET_MAX_RESERVED_REGIONS) {
                        mre_violet_region_t *dst=&info->reserved[info->reserved_region_count++];
                        dst->base=reg.address;dst->size=reg.size;
                    } else ++info->dynamic_reserved_count;
                } else {
                    /* Dynamic-size and firmware-owned carveouts cannot be
                       safely mapped until the bootloader supplies placement. */
                    ++info->dynamic_reserved_count;
                }
            }
        } else {
            ++info->dynamic_reserved_count;
        }
    }
    info->memory_map_valid=(info->memory_region_count!=0u &&
                            info->dynamic_reserved_count==0u)?1u:0u;
    return MRE_OK;
}
mre_status_t mre_violet_discover(const mre_fdt_t *fdt,mre_violet_boot_info_t *info)
{
    mre_fdt_node_t node;
    mre_status_t st;
    if(fdt==NULL || info==NULL) return MRE_E_INVALID;
    memset(info,0,sizeof(*info)); info->dtb_size=fdt->total_size;
    /* Candidate Violet marker: confirm this string against a live handset DTB before relaxing the gate. */
    info->board_identity_verified=root_has_compatible(fdt,"xiaomi,violet")?1u:0u;

    st=find_one(fdt,"arm,gic-v3",&node);
    if(st==MRE_OK) {
        info->gic.version=MRE_GIC_V3;
        if(get_reg_or_fail(fdt,node,0,&info->gic.distributor_base,&info->gic.distributor_size)!=MRE_OK ||
           get_reg_or_fail(fdt,node,1,&info->gic.redistributor_base,&info->gic.redistributor_size)!=MRE_OK)
            return MRE_E_INVALID;
        info->gic_found=1;
    } else {
        st=find_one(fdt,"arm,cortex-a15-gic",&node);
        if(st==MRE_OK) {
            info->gic.version=MRE_GIC_V2;
            if(get_reg_or_fail(fdt,node,0,&info->gic.distributor_base,&info->gic.distributor_size)!=MRE_OK ||
               get_reg_or_fail(fdt,node,1,&info->gic.cpu_interface_base,&info->gic.cpu_interface_size)!=MRE_OK)
                return MRE_E_INVALID;
            info->gic_found=1;
        }
    }

    st=find_one(fdt,"qcom,msm-geni-console",&node);
    if(st!=MRE_OK) st=find_one(fdt,"qcom,geni-debug-uart",&node);
    if(st!=MRE_OK) st=find_one(fdt,"qcom,geni-uart",&node);
    if(st==MRE_OK) {
        const void *interrupts; uint32_t length;
        uint32_t cells=3u;
        if(get_reg_or_fail(fdt,node,0,&info->uart_base,&info->uart_size)!=MRE_OK)
            return MRE_E_INVALID;
        info->uart_found=1;
        if(mre_fdt_get_interrupt_cells(fdt,node,&cells)!=MRE_OK) cells=3u;
        if(mre_fdt_get_property(fdt,node,"interrupts",&interrupts,&length)==MRE_OK &&
           cells>=3u && length>=cells*4u) {
            const uint8_t *b=(const uint8_t *)interrupts;
            uint32_t type=fdt_be32(b);
            uint32_t number=fdt_be32(b+4u);
            if(type==0u && number<992u) info->uart_irq=32u+number;
        }
    }

    st=find_one(fdt,"arm,armv8-timer",&node);
    if(st==MRE_OK) {
        const void *interrupts; uint32_t length, cells=3u;
        if(mre_fdt_get_interrupt_cells(fdt,node,&cells)!=MRE_OK) cells=3u;
        if(mre_fdt_get_property(fdt,node,"interrupts",&interrupts,&length)==MRE_OK &&
           cells>=3u && length>=cells*4u && (length%(cells*4u))==0u) {
            const uint8_t *b=(const uint8_t *)interrupts;
            uint32_t stride=cells*4u;
            uint32_t tuple=(length>=stride*2u)?1u:0u;
            uint32_t type=fdt_be32(b+tuple*stride);
            uint32_t number=fdt_be32(b+tuple*stride+4u);
            if(type==1u && number<16u) {
                info->timer_irq=16u+number;
                info->timer_found=1;
            }
        }
    }
    info->display_status=discover_display(fdt,info);
    st=discover_memory(fdt,info);
    if(st!=MRE_OK) return st;
    info->valid=1;
    return (info->gic_found || info->uart_found || info->timer_found)?MRE_OK:MRE_E_NOT_FOUND;
}
const mre_violet_boot_info_t *mre_violet_boot_info(void) { return &g_info; }

const mre_kernel_boot_report_t *mre_kernel_boot_report(void) { return &g_boot_report; }

mre_status_t mre_kernel_enable_mmu(void)
{
    mre_status_t st;
    if(g_boot_report.mmu_build_status!=MRE_OK) return g_boot_report.mmu_build_status;
    st=mre_mmu_enable();
    if(st==MRE_OK) g_boot_report.mmu_enabled=1u;
    return st;
}

#if defined(MRE_ENABLE_STANDALONE_ENTRY)
void mre_kernel_main(uint64_t dtb_address)
{
    mre_fdt_t fdt;
    mre_status_t st;
#if defined(__aarch64__)
    extern uint8_t __kernel_start[],__kernel_end[];
    extern uint8_t __text_start[],__text_end[];
    extern uint8_t __rodata_start[],__rodata_end[];
    mre_violet_kernel_layout_t layout;
#endif
    memset(&g_boot_report,0,sizeof(g_boot_report));
    if(dtb_address==0u) {
        g_boot_report.fdt_status=MRE_E_INVALID;
        for(;;) mre_arch_wfe();
    }
    st=mre_fdt_open(&fdt,(const void *)(uintptr_t)dtb_address,2u*1024u*1024u);
    g_boot_report.fdt_status=st;
    if(st!=MRE_OK) for(;;) mre_arch_wfe();
    st=mre_violet_discover(&fdt,&g_info);
    g_boot_report.discovery_status=st;
    if(st!=MRE_OK) for(;;) mre_arch_wfe();

    g_boot_report.mmu_prerequisite_status=mre_violet_mmu_prerequisites(&g_info);
    if(g_boot_report.mmu_prerequisite_status==MRE_OK) {
#if defined(__aarch64__)
        layout.image_start=(uint64_t)(uintptr_t)__kernel_start;
        layout.image_end=(uint64_t)(uintptr_t)__kernel_end;
        layout.text_start=(uint64_t)(uintptr_t)__text_start;
        layout.text_end=(uint64_t)(uintptr_t)__text_end;
        layout.rodata_start=(uint64_t)(uintptr_t)__rodata_start;
        layout.rodata_end=(uint64_t)(uintptr_t)__rodata_end;
        g_boot_report.mmu_build_status=mre_violet_build_page_tables(&g_info,&layout);
#else
        g_boot_report.mmu_build_status=MRE_E_UNSUPPORTED;
#endif
    } else {
        g_boot_report.mmu_build_status=g_boot_report.mmu_prerequisite_status;
    }

    /* MMU enable remains an explicit action after the live Violet DTB,
       boot handoff and reserved-memory map have passed validation. */
    for(;;) mre_arch_wfe();
}
#endif /* MRE_ENABLE_STANDALONE_ENTRY */

static uint64_t page_down(uint64_t v) { return v & ~((uint64_t)MRE_PAGE_SIZE-1u); }
static uint64_t page_up(uint64_t v)
{
    if(v>UINT64_MAX-(MRE_PAGE_SIZE-1u)) return UINT64_MAX;
    return (v+MRE_PAGE_SIZE-1u)&~((uint64_t)MRE_PAGE_SIZE-1u);
}
static int overlaps(uint64_t a,uint64_t ae,uint64_t b,uint64_t be)
{ return a<be && b<ae; }
static int range_is_usable(const mre_violet_boot_info_t *info,uint64_t base,uint64_t size)
{
    uint64_t end;
    uint32_t i;
    int contained=0;
    if(size==0u || base>UINT64_MAX-size) return 0;
    end=base+size;
    for(i=0;i<info->memory_region_count;++i) {
        uint64_t mb=info->memory[i].base, me=mb+info->memory[i].size;
        if(base>=mb && end<=me) { contained=1; break; }
    }
    if(!contained) return 0;
    for(i=0;i<info->reserved_region_count;++i) {
        uint64_t rb=page_down(info->reserved[i].base);
        uint64_t re=page_up(info->reserved[i].base+info->reserved[i].size);
        if(re==UINT64_MAX || overlaps(base,end,rb,re)) return 0;
    }
    return 1;
}
mre_status_t mre_violet_mmu_prerequisites(const mre_violet_boot_info_t *info)
{
    uint32_t i;
    if(info==NULL || info->valid==0u ||
       info->memory_region_count>MRE_VIOLET_MAX_MEMORY_REGIONS ||
       info->reserved_region_count>MRE_VIOLET_MAX_RESERVED_REGIONS)
        return MRE_E_INVALID;
    if(info->board_identity_verified==0u || info->memory_map_valid==0u ||
       info->memory_region_count==0u || info->dynamic_reserved_count!=0u)
        return MRE_E_UNSUPPORTED;
    for(i=0;i<info->memory_region_count;++i) {
        uint64_t b=info->memory[i].base,s=info->memory[i].size;
        if(s==0u || b>UINT64_MAX-s || b+s>(1ull<<48)) return MRE_E_INVALID;
    }
    for(i=0;i<info->reserved_region_count;++i) {
        uint64_t b=info->reserved[i].base,s=info->reserved[i].size;
        if(s==0u || b>UINT64_MAX-s || b+s>(1ull<<48)) return MRE_E_INVALID;
    }
    return MRE_OK;
}
static mre_status_t map_mmio(uint64_t base,uint64_t size)
{
    uint64_t start,end;
    mre_mmu_region_t r;
    if(base==0u || size==0u || base>UINT64_MAX-size) return MRE_E_INVALID;
    start=page_down(base);end=page_up(base+size);
    if(end==UINT64_MAX || end<=start) return MRE_E_INVALID;
    r.virtual_address=start;r.physical_address=start;r.size=end-start;
    r.type=MRE_MEMORY_DEVICE;r.writable=1u;r.executable=0u;
    return mre_mmu_map_range(&r);
}
static mre_status_t map_usable_ram(const mre_violet_boot_info_t *info)
{
    uint32_t m,i;
    for(m=0;m<info->memory_region_count;++m) {
        uint64_t cursor=page_up(info->memory[m].base);
        uint64_t end=page_down(info->memory[m].base+info->memory[m].size);
        while(cursor<end) {
            uint64_t skip_to=cursor,next_reserved=end;
            for(i=0;i<info->reserved_region_count;++i) {
                uint64_t rb=page_down(info->reserved[i].base);
                uint64_t re=page_up(info->reserved[i].base+info->reserved[i].size);
                if(re<=cursor || rb>=end) continue;
                if(rb<=cursor) { if(re>skip_to) skip_to=re; }
                else if(rb<next_reserved) next_reserved=rb;
            }
            if(skip_to>cursor) { cursor=(skip_to<end)?skip_to:end; continue; }
            if(next_reserved<=cursor) return MRE_E_INVALID;
            {
                mre_mmu_region_t r;
                r.virtual_address=cursor;r.physical_address=cursor;
                r.size=next_reserved-cursor;r.type=MRE_MEMORY_NORMAL_WB;
                r.writable=1u;r.executable=0u;
                if(r.size!=0u) {
                    mre_status_t st=mre_mmu_map_range(&r);
                    if(st!=MRE_OK) return st;
                }
            }
            cursor=next_reserved;
        }
    }
    return MRE_OK;
}
mre_status_t mre_violet_build_page_tables(const mre_violet_boot_info_t *info,
                                           const mre_violet_kernel_layout_t *layout)
{
    mre_status_t st;
    uint64_t text_start,text_end,ro_start,ro_end;
    mre_mmu_region_t r;
    if(layout==NULL || layout->image_start>=layout->image_end ||
       layout->text_start>=layout->text_end ||
       layout->rodata_start>=layout->rodata_end) return MRE_E_INVALID;
    st=mre_violet_mmu_prerequisites(info);if(st!=MRE_OK) return st;
    if(!range_is_usable(info,layout->image_start,layout->image_end-layout->image_start))
        return MRE_E_PERMISSION;
    text_start=page_down(layout->text_start);text_end=page_up(layout->text_end);
    ro_start=page_down(layout->rodata_start);ro_end=page_up(layout->rodata_end);
    if(text_end==UINT64_MAX || ro_end==UINT64_MAX ||
       !range_is_usable(info,text_start,text_end-text_start) ||
       !range_is_usable(info,ro_start,ro_end-ro_start)) return MRE_E_PERMISSION;

    mre_mmu_reset();
    st=map_usable_ram(info);if(st!=MRE_OK) return st;
    r.virtual_address=text_start;r.physical_address=text_start;r.size=text_end-text_start;
    r.type=MRE_MEMORY_NORMAL_WB;r.writable=0u;r.executable=1u;
    st=mre_mmu_map_range(&r);if(st!=MRE_OK) return st;
    r.virtual_address=ro_start;r.physical_address=ro_start;r.size=ro_end-ro_start;
    r.writable=0u;r.executable=0u;
    st=mre_mmu_map_range(&r);if(st!=MRE_OK) return st;

    if(info->gic_found) {
        st=map_mmio(info->gic.distributor_base,info->gic.distributor_size);
        if(st!=MRE_OK) return st;
        if(info->gic.version==MRE_GIC_V3)
            st=map_mmio(info->gic.redistributor_base,info->gic.redistributor_size);
        else if(info->gic.version==MRE_GIC_V2)
            st=map_mmio(info->gic.cpu_interface_base,info->gic.cpu_interface_size);
        if(st!=MRE_OK) return st;
    }
    if(info->uart_found) {
        st=map_mmio(info->uart_base,info->uart_size);
        if(st!=MRE_OK) return st;
    }
    if(info->display.mdss_found) {
        st=map_mmio(info->display.mdss_base,info->display.mdss_size);
        if(st!=MRE_OK) return st;
    }
    if(info->display.dpu_found) {
        st=map_mmio(info->display.dpu_base,info->display.dpu_size);
        if(st!=MRE_OK) return st;
        st=map_mmio(info->display.dpu_vbif_base,info->display.dpu_vbif_size);
        if(st!=MRE_OK) return st;
    }
    if(info->display.dsi_found) {
        st=map_mmio(info->display.dsi_base,info->display.dsi_size);
        if(st!=MRE_OK) return st;
    }
    if(info->display.phy_found) {
        st=map_mmio(info->display.phy_base,info->display.phy_size);
        if(st!=MRE_OK) return st;
        st=map_mmio(info->display.phy_lane_base,info->display.phy_lane_size);
        if(st!=MRE_OK) return st;
        st=map_mmio(info->display.phy_pll_base,info->display.phy_pll_size);
        if(st!=MRE_OK) return st;
    }
    if(info->display.dispcc_found) {
        st=map_mmio(info->display.dispcc_base,info->display.dispcc_size);
        if(st!=MRE_OK) return st;
    }
    return MRE_OK;
}
