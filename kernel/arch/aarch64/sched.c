/* VQEAF AArch64 preemptive scheduler.
 *
 * Reuses the arch-neutral VqeafTask model and constants from scheduler.h, but
 * the context frame, switch and EL0 entry are AArch64-specific. The timer IRQ
 * vector saves a 272-byte frame (x0-x30, ELR, SPSR, SP_EL0) on the interrupted
 * task's kernel stack; vqeaf_a64_sched_switch() returns the frame to resume and
 * the vector restores from it and erets. Kernel tasks run at EL1h, user tasks
 * at EL0t with their own TTBR0 root (kernel slot 0 shared, EL1-only leaves).
 *
 * This is the independent-OS counterpart of kernel/core/scheduler.c (x86_64);
 * that file is left untouched so the x86 UEFI kernel keeps its green gate. */
#include "vqeaf/scheduler.h"
#include "vqeaf/memory.h"
#include "vqeaf/vm.h"
#include "vqeaf/uart_aarch64.h"
#include "vqeaf/arch_aarch64.h"
#include "vqeaf/window.h"
#include "vqeaf/vfs.h"

#define FRAME_BYTES 272u
#define SPSR_EL1H_IRQ 0x345ull  /* EL1h, DAIF: D/A/F masked, IRQ enabled */
#define SPSR_EL0T_IRQ 0x340ull  /* EL0t, IRQ enabled */

static VqeafTask tasks[VQEAF_TASK_CAPACITY];
static int32_t current = -1;
static uint64_t ticks;
static uint64_t kernel_root;
static int sched_active;
static uint64_t tasks_created, tasks_exited, switches;
static int user_entered;

void vqeaf_sched_init(void)
{
    for(unsigned i = 0; i < VQEAF_TASK_CAPACITY; i++) {
        tasks[i].id = 0; tasks[i].state = VQEAF_TASK_UNUSED;
        tasks[i].kind = VQEAF_TASK_KERNEL; tasks[i].ticks = 0;
        tasks[i].rsp = 0; tasks[i].root = 0; tasks[i].kernel_stack = 0;
        tasks[i].stack_pages = 0; tasks[i].user_stack = 0;
        tasks[i].user_stack_pages = 0; tasks[i].fn = 0; tasks[i].arg = 0;
        tasks[i].cred.uid = VQEAF_UID_SYSTEM;
        tasks[i].cred.gid = VQEAF_GID_SYSTEM;
        tasks[i].cred.caps = VQEAF_CAP_ALL;
    }
    current = -1; ticks = 0; sched_active = 0;
    tasks_created = 0; tasks_exited = 0; switches = 0; user_entered = 0;
}

void vqeaf_sched_set_kernel_root(uint64_t root) { kernel_root = root; }
int32_t vqeaf_sched_current(void) { return current; }
int vqeaf_task_state(int slot)
{
    if(slot < 0 || slot >= (int)VQEAF_TASK_CAPACITY) return -1;
    return (int)tasks[slot].state;
}
uint64_t vqeaf_timer_ticks(void) { return ticks; }
int vqeaf_a64_sched_active(void) { return sched_active; }

uint64_t vqeaf_task_stack_base(int slot)
{
    if(slot < 0 || slot >= (int)VQEAF_TASK_CAPACITY) return 0;
    if(tasks[slot].state == VQEAF_TASK_UNUSED) return 0;
    return tasks[slot].kernel_stack;
}

/* Initial frame layout matches vectors.S save_all:
 * [0..30]=x0..x30, [31]=ELR, [32]=SPSR, [33]=SP_EL0 (34 * 8 = 272 bytes). */
static void build_initial_frame(VqeafTask *t, uint64_t entry, uint64_t sp_el0,
                                uint64_t spsr, uint64_t fn, uint64_t arg)
{
    uint64_t top = t->kernel_stack + t->stack_pages * VQEAF_PAGE_SIZE;
    uint64_t *f = (uint64_t *)(top - FRAME_BYTES);
    for(int i = 0; i < 34; i++) f[i] = 0;
    f[19] = fn;     /* x19: trampoline function */
    f[20] = arg;    /* x20: trampoline argument */
    f[31] = entry;  /* ELR */
    f[32] = spsr;   /* SPSR */
    f[33] = sp_el0; /* SP_EL0 */
    t->rsp = (uint64_t)f;
}

/* The body is called with IRQ masked: slot selection, page allocation,
 * frame construction and tasks_created++ must be one atomic operation.
 * A timer IRQ in the middle would let a second creator claim the same
 * UNUSED slot (two counts, one task), which later prevents shutdown. */
static int task_create_locked(VqeafTaskFn fn, void *arg)
{
    if(!fn) return -1;
    int slot = -1;
    for(unsigned i = 0; i < VQEAF_TASK_CAPACITY; i++)
        if(tasks[i].state == VQEAF_TASK_UNUSED) { slot = (int)i; break; }
    if(slot < 0) return -2;
    uint64_t run = vqeaf_pmm_alloc_pages(VQEAF_TASK_STACK_PAGES + 1);
    if(!run) return -3;
    VqeafTask *t = &tasks[slot];
    t->id = (uint32_t)(slot + 1); t->state = VQEAF_TASK_READY; t->ticks = 0;
    t->kind = VQEAF_TASK_KERNEL;
    t->kernel_stack = run + VQEAF_PAGE_SIZE; t->stack_pages = VQEAF_TASK_STACK_PAGES;
    t->fn = fn; t->arg = arg; t->root = 0; t->user_stack = 0; t->user_stack_pages = 0;
    t->cred.uid = VQEAF_UID_SYSTEM; t->cred.gid = VQEAF_GID_SYSTEM;
    t->cred.caps = VQEAF_CAP_ALL;
    if(kernel_root) vqeaf_vm_unmap(kernel_root, run); /* leave the guard page unmapped */
    build_initial_frame(t, (uint64_t)vqeaf_a64_task_trampoline, 0, SPSR_EL1H_IRQ,
                        (uint64_t)fn, (uint64_t)arg);
    tasks_created++;
    return slot;
}

int vqeaf_task_create(VqeafTaskFn fn, void *arg)
{
    uint64_t daif = vqeaf_a64_irq_save();
    int r = task_create_locked(fn, arg);
    vqeaf_a64_irq_restore(daif);
    return r;
}

static int task_create_user_locked(uint64_t root, uint64_t entry,
                                   uint64_t user_rsp_top, const VqeafCred *cred)
{
    if(!root || entry < VQEAF_USER_REGION_BASE || entry >= VQEAF_USER_REGION_LIMIT) return -1;
    int slot = -1;
    for(unsigned i = 0; i < VQEAF_TASK_CAPACITY; i++)
        if(tasks[i].state == VQEAF_TASK_UNUSED) { slot = (int)i; break; }
    if(slot < 0) return -2;
    uint64_t run = vqeaf_pmm_alloc_pages(VQEAF_TASK_STACK_PAGES + 1);
    if(!run) return -3;
    uint64_t urun = vqeaf_pmm_alloc_pages(VQEAF_USER_STACK_PAGES);
    if(!urun) return -4;
    VqeafTask *t = &tasks[slot];
    t->id = (uint32_t)(slot + 1); t->state = VQEAF_TASK_READY; t->ticks = 0;
    t->kind = VQEAF_TASK_USER;
    t->kernel_stack = run + VQEAF_PAGE_SIZE; t->stack_pages = VQEAF_TASK_STACK_PAGES;
    t->user_stack = urun; t->user_stack_pages = VQEAF_USER_STACK_PAGES;
    t->fn = 0; t->arg = 0; t->root = root;
    t->cred = *cred;
    for(uint64_t i = 0; i < VQEAF_USER_STACK_PAGES; i++)
        if(vqeaf_vm_map(root, VQEAF_USER_STACK_BASE + i * VQEAF_PAGE_SIZE,
                        urun + i * VQEAF_PAGE_SIZE, VQEAF_VM_WRITE | VQEAF_VM_USER) != 0)
            return -5;
    if(kernel_root) vqeaf_vm_unmap(kernel_root, run);
    build_initial_frame(t, entry, user_rsp_top, SPSR_EL0T_IRQ, 0, 0);
    tasks_created++;
    return slot;
}

/* One IRQ-shielded creation path shared by the boot-only system helper and
 * the validated app credential helper. Keeping the masking here means neither
 * caller can accidentally create a task with IRQ preemption enabled. */
static int task_create_user_with_cred(uint64_t root, uint64_t entry,
                                      uint64_t user_rsp_top, const VqeafCred *cred)
{
    uint64_t daif = vqeaf_a64_irq_save();
    int r = task_create_user_locked(root, entry, user_rsp_top, cred);
    vqeaf_a64_irq_restore(daif);
    return r;
}

/* .qen apps are never given the system identity: AMS calls the credential
 * variant below, and this wrapper is reserved for the kernel's own boot-time
 * EL0 self-tests (ELF/ELF-IPC/.qen/surface/launcher harnesses). */
int vqeaf_task_create_user(uint64_t root, uint64_t entry, uint64_t user_rsp_top)
{
    static const VqeafCred system_cred = VQEAF_CRED_SYSTEM_INIT;
    return task_create_user_with_cred(root, entry, user_rsp_top, &system_cred);
}

int vqeaf_task_create_user_cred(uint64_t root, uint64_t entry,
                                uint64_t user_rsp_top, const VqeafCred *cred)
{
    /* Fail closed before touching a task slot: an app that asks for the system
     * uid, CAP_SYSTEM or CAP_FS_ADMIN is rejected, not silently downgraded. */
    if(!vqeaf_sec_user_cred_ok(cred)) return -6;
    return task_create_user_with_cred(root, entry, user_rsp_top, cred);
}

const VqeafCred *vqeaf_task_cred_ptr(int slot)
{
    if(slot < 0 || slot >= (int)VQEAF_TASK_CAPACITY) return 0;
    if(tasks[slot].state == VQEAF_TASK_UNUSED) return 0;
    return &tasks[slot].cred;
}

void vqeaf_task_exit(void)
{
    __asm__ volatile("msr daifset, #2" ::: "memory");
    if(current >= 0 && tasks[current].state != VQEAF_TASK_EXITED) {
        /* A dead process must not leave layers on screen or keep input focus.
         * This is the window manager's counterpart of app death. */
        vqeaf_wm_close_owner(current);
        vqeaf_vfs_release_task(current);
        tasks[current].state = VQEAF_TASK_EXITED;
        tasks_exited++;
    }
    /* Let the timer IRQ preempt us away to another runnable task. */
    __asm__ volatile("msr daifclr, #2" ::: "memory");
    for(;;) __asm__ volatile("wfi");
}

static int pick_next(int from)
{
    for(unsigned n = 1; n <= VQEAF_TASK_CAPACITY; n++) {
        unsigned i = (unsigned)((from + (int)n + VQEAF_TASK_CAPACITY) % VQEAF_TASK_CAPACITY);
        if(tasks[i].state == VQEAF_TASK_READY && tasks[i].rsp) return (int)i;
    }
    return -1;
}

/* Terminal test derived from the task table, not from counters.
 *
 * A preemptible task_create_user() used to let two creators claim the same
 * UNUSED slot, so tasks_created became larger than the number of real tasks.
 * The old "tasks_exited >= tasks_created" predicate could then never become
 * true and QEMU spun forever in wfi instead of issuing PSCI SYSTEM_OFF.
 * Counting occupied slots that are all EXITED is exact, and because
 * pick_next() already proved there is no READY task, it cannot fire early. */
static uint32_t exited_task_count(void)
{
    uint32_t n = 0;
    for(unsigned i = 0; i < VQEAF_TASK_CAPACITY; i++) {
        if(tasks[i].state == VQEAF_TASK_UNUSED) continue;
        if(tasks[i].state != VQEAF_TASK_EXITED) return 0;
        n++;
    }
    return n;
}

void vqeaf_sched_start(void)
{
    int first = -1;
    for(unsigned i = 0; i < VQEAF_TASK_CAPACITY; i++)
        if(tasks[i].state == VQEAF_TASK_READY && tasks[i].rsp) { first = (int)i; break; }
    if(first < 0) {
        vqeaf_uart_write("SCHED: no runnable task at start\r\n");
        for(;;) __asm__ volatile("wfi");
    }
    current = first; tasks[first].state = VQEAF_TASK_RUNNING;
    sched_active = 1;
    if(tasks[first].root) vqeaf_vm_activate(tasks[first].root);
    vqeaf_uart_write("SCHED: entering first task\r\n");
    vqeaf_a64_enter_first(tasks[first].rsp);
}

uint64_t vqeaf_a64_sched_switch(uint64_t frame_base)
{
    ticks++;
    int cur = current;
    if(cur >= 0 && tasks[cur].state == VQEAF_TASK_RUNNING) {
        tasks[cur].rsp = frame_base;
        tasks[cur].ticks++;
        tasks[cur].state = VQEAF_TASK_READY;
    }
    int next = pick_next(cur);
    if(next < 0) {
        uint32_t done = exited_task_count();
        if(done > 0) {
            vqeaf_uart_write("SCHED: all tasks completed (exits=");
            vqeaf_uart_u64((uint64_t)done);
            vqeaf_uart_write(", switches=");
            vqeaf_uart_u64(switches);
            vqeaf_uart_write(")\r\n");
            /* Counters are still audited: any drift is reported instead of
             * silently turning into a hang. The gate fails fast on the log. */
            if((uint64_t)done != tasks_created || (uint64_t)done != tasks_exited)
                vqeaf_uart_write("SCHED: warning: task counters drifted\r\n");
            if(switches > 0)
                vqeaf_uart_write("SCHED: context switches occurred\r\n");
            vqeaf_a64_power_off();
        }
        /* Defensive: work remains, so keep the current task runnable. An
         * already-EXITED slot must never be resurrected as RUNNING. */
        if(cur >= 0 && tasks[cur].state != VQEAF_TASK_EXITED) {
            tasks[cur].state = VQEAF_TASK_RUNNING;
            return frame_base;
        }
        vqeaf_uart_write("SCHED: no runnable task\r\n");
        for(;;) __asm__ volatile("wfi");
    }
    if(next != cur) switches++;
    current = next; tasks[next].state = VQEAF_TASK_RUNNING;
    if(tasks[next].kind == VQEAF_TASK_USER && !user_entered) {
        user_entered = 1;
        vqeaf_uart_write("SCHED: switch to EL0 task\r\n");
    }
    uint64_t want = tasks[next].root ? tasks[next].root : kernel_root;
    if(want && want != vqeaf_vm_current_root())
        vqeaf_vm_activate(want);
    return tasks[next].rsp;
}