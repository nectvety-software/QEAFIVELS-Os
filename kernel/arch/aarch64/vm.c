/* VQEAF AArch64 virtual memory (4 KiB granule, 48-bit VA, 4-level walk).
 *
 * Mirrors the x86_64 vm.h contract so the portable core (scheduler, ELF loader)
 * can be reused unchanged. A single TTBR0_EL1 table covers both the identity-
 * mapped kernel (low VA) and the per-task user region at 0x10000000000; EL0
 * isolation is enforced by the leaf AP bits (kernel pages are EL1-only).
 * TTBR1 walks are disabled (TCR_EL1.EPD1). */
#include "vqeaf/vm.h"
#include "vqeaf/memory.h"
#include "vqeaf/arch_aarch64.h"

#define ADDR_MASK 0x0000fffffffff000ull /* output address bits [47:12] */
#define PTE_VALID (1ull << 0)
#define PTE_TABLE (1ull << 1)           /* table (level<3) / page (level 3) */
#define PTE_AF    (1ull << 10)
#define PTE_SH_ISH (3ull << 8)
#define PTE_PXN   (1ull << 53)
#define PTE_UXN   (1ull << 54)

static uint64_t *table(uint64_t physical) { return (uint64_t *)(uintptr_t)physical; }

static void tlb_flush_va(uint64_t va)
{
    __asm__ volatile("dsb ishst" ::: "memory");
    __asm__ volatile("tlbi vale1, %0" :: "r"(va >> 12) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

static uint64_t leaf(uint64_t pa, uint64_t flags)
{
    uint64_t e = (pa & ADDR_MASK) | PTE_VALID | PTE_TABLE | PTE_AF;
    if(flags & VQEAF_VM_CACHE_DISABLE)
        e |= (1ull << 2);                 /* AttrIndx=1: device-nGnRE */
    else
        e |= (0ull << 2) | PTE_SH_ISH;    /* AttrIndx=0: normal WB, inner shareable */
    int write = (flags & VQEAF_VM_WRITE) != 0;
    int user  = (flags & VQEAF_VM_USER) != 0;
    uint64_t ap = write ? (user ? 1ull : 0ull) : (user ? 3ull : 2ull);
    e |= (ap << 6);
    if(flags & VQEAF_VM_NO_EXECUTE)
        e |= PTE_PXN | PTE_UXN;
    return e;
}

static uint64_t *child(uint64_t *parent, unsigned index)
{
    if(parent[index] & PTE_VALID)
        return table(parent[index] & ADDR_MASK);
    uint64_t p = vqeaf_pmm_alloc_page();
    if(!p) return 0;
    uint64_t *t = table(p);
    for(unsigned i = 0; i < 512; i++) t[i] = 0;
    parent[index] = (p & ADDR_MASK) | PTE_VALID | PTE_TABLE;
    return t;
}

uint64_t vqeaf_vm_create_root(void)
{
    uint64_t p = vqeaf_pmm_alloc_page();
    if(!p) return 0;
    uint64_t *t = table(p);
    for(unsigned i = 0; i < 512; i++) t[i] = 0;
    return p;
}

uint64_t vqeaf_vm_user_root(uint64_t kernel_root)
{
    if(!kernel_root) return 0;
    uint64_t p = vqeaf_pmm_alloc_page();
    if(!p) return 0;
    uint64_t *t = table(p);
    for(unsigned i = 0; i < 512; i++) t[i] = 0;
    /* Share PGD slot 0 (identity kernel + devices, EL1-only leaves); the task
     * gets its own slot 2 subtree for the user region at 0x10000000000. */
    t[0] = table(kernel_root)[0];
    return p;
}

int vqeaf_vm_map(uint64_t root, uint64_t va, uint64_t pa, uint64_t flags)
{
    if(!root || (va & 0xfff) || (pa & 0xfff) || va >= 0x0001000000000000ull) return -1;
    uint64_t *pgd = table(root);
    uint64_t *pud = child(pgd, (unsigned)((va >> 39) & 511)); if(!pud) return -2;
    uint64_t *pmd = child(pud, (unsigned)((va >> 30) & 511)); if(!pmd) return -2;
    uint64_t *pt  = child(pmd, (unsigned)((va >> 21) & 511)); if(!pt)  return -2;
    unsigned i = (unsigned)((va >> 12) & 511);
    if(pt[i] & PTE_VALID) return -3;
    pt[i] = leaf(pa, flags);
    tlb_flush_va(va);
    return 0;
}

int vqeaf_vm_map_or_update(uint64_t root, uint64_t va, uint64_t pa, uint64_t flags)
{
    int r = vqeaf_vm_map(root, va, pa, flags);
    if(r != -3) return r;
    uint64_t *pgd = table(root);
    uint64_t *pud = table(pgd[(va >> 39) & 511] & ADDR_MASK);
    uint64_t *pmd = table(pud[(va >> 30) & 511] & ADDR_MASK);
    uint64_t *pt  = table(pmd[(va >> 21) & 511] & ADDR_MASK);
    unsigned i = (unsigned)((va >> 12) & 511);
    if((pt[i] & ADDR_MASK) != (pa & ADDR_MASK)) return -4;
    /* Break-before-make: invalidate, then rewrite the leaf with OR'd flags. */
    pt[i] = 0;
    tlb_flush_va(va);
    pt[i] = leaf(pa, flags);
    tlb_flush_va(va);
    return 0;
}

int vqeaf_vm_map_range(uint64_t root, uint64_t va, uint64_t pa, uint64_t pages, uint64_t flags)
{
    if((va & 0xfff) || (pa & 0xfff)) return -1;
    for(uint64_t i = 0; i < pages; i++) {
        int r = vqeaf_vm_map(root, va + i * VQEAF_PAGE_SIZE, pa + i * VQEAF_PAGE_SIZE, flags);
        if(r) return r;
    }
    return 0;
}

int vqeaf_vm_map_range_or_update(uint64_t root, uint64_t va, uint64_t pa, uint64_t pages, uint64_t flags)
{
    for(uint64_t i = 0; i < pages; i++) {
        int r = vqeaf_vm_map_or_update(root, va + i * VQEAF_PAGE_SIZE, pa + i * VQEAF_PAGE_SIZE, flags);
        if(r) return r;
    }
    return 0;
}

int vqeaf_vm_unmap(uint64_t root, uint64_t va)
{
    if(!root || (va & 0xfff) || va >= 0x0001000000000000ull) return -1;
    uint64_t *pgd = table(root);
    if(!(pgd[(va >> 39) & 511] & PTE_VALID)) return -2;
    uint64_t *pud = table(pgd[(va >> 39) & 511] & ADDR_MASK);
    if(!(pud[(va >> 30) & 511] & PTE_VALID)) return -2;
    uint64_t *pmd = table(pud[(va >> 30) & 511] & ADDR_MASK);
    if(!(pmd[(va >> 21) & 511] & PTE_VALID)) return -2;
    uint64_t *pt = table(pmd[(va >> 21) & 511] & ADDR_MASK);
    unsigned i = (unsigned)((va >> 12) & 511);
    if(!(pt[i] & PTE_VALID)) return -3;
    pt[i] = 0;
    tlb_flush_va(va);
    return 0;
}

void vqeaf_vm_activate(uint64_t root)
{
    if(!root) return;
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(root) : "memory");
    __asm__ volatile("tlbi vmalle1" ::: "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
}

uint64_t vqeaf_vm_current_root(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v));
    return v & ADDR_MASK;
}

/* True only if every page covering [va, va+len) lies in the user region and is
 * present + EL0-accessible (AP[6]=1). The IPC syscall path uses this to reject
 * bad user pointers before the kernel dereferences them, so a malicious EL0
 * task cannot trick the kernel into touching kernel memory or faulting. */
int vqeaf_vm_user_range_mapped(uint64_t root, uint64_t va, uint64_t len)
{
    if(!root || !len) return 0;
    if(va < VQEAF_USER_REGION_BASE || va + len > VQEAF_USER_REGION_LIMIT) return 0;
    uint64_t *pgd = table(root);
    uint64_t end = va + len;
    for(uint64_t a = va & ~0xfffull; a < end; a += VQEAF_PAGE_SIZE) {
        uint64_t e = pgd[(a >> 39) & 511];
        if(!(e & PTE_VALID)) return 0;
        uint64_t *pud = table(e & ADDR_MASK);
        e = pud[(a >> 30) & 511]; if(!(e & PTE_VALID)) return 0;
        uint64_t *pmd = table(e & ADDR_MASK);
        e = pmd[(a >> 21) & 511]; if(!(e & PTE_VALID)) return 0;
        uint64_t *pt = table(e & ADDR_MASK);
        e = pt[(a >> 12) & 511];
        if(!(e & PTE_VALID)) return 0;
        if(!(e & (1ull << 6))) return 0;  /* AP[6]=0 => EL1-only, reject */
    }
    return 1;
}

void vqeaf_aarch64_mmu_enable(void)
{
    /* MAIR: Attr0 = 0xFF (normal WB), Attr1 = 0x04 (device-nGnRE). */
    uint64_t mair = 0xFFull | (0x04ull << 8);
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));
    /* TCR: T0SZ=16 (48-bit VA), 4 KiB granule, WB inner-shareable, IPS=40-bit,
     * EPD1=1 (disable TTBR1 walks; everything lives in TTBR0). */
    uint64_t tcr = (16ull << 0) | (1ull << 8) | (1ull << 10) | (3ull << 12)
                 | (0ull << 14) | (1ull << 23) | (2ull << 32);
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr));
    __asm__ volatile("isb" ::: "memory");
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ull << 0) | (1ull << 2) | (1ull << 12); /* M | C | I */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr) : "memory");
    __asm__ volatile("isb" ::: "memory");
}