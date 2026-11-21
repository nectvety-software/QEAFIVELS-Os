#ifndef VQEAF_SCHEDULER_H
#define VQEAF_SCHEDULER_H
#include <stdint.h>
#include "security.h"
#define VQEAF_TASK_CAPACITY 32u
#define VQEAF_TASK_STACK_PAGES 4u
#define VQEAF_USER_STACK_PAGES 4u
#define VQEAF_USER_STACK_BASE 0x10080000000ull /* above the 16 MB ELF region */
typedef enum { VQEAF_TASK_UNUSED=0,VQEAF_TASK_READY=1,VQEAF_TASK_RUNNING=2,VQEAF_TASK_BLOCKED=3,VQEAF_TASK_EXITED=4 } VqeafTaskState;
typedef enum { VQEAF_TASK_KERNEL=0,VQEAF_TASK_USER=1 } VqeafTaskKind;
typedef void (*VqeafTaskFn)(void*);
typedef struct {
 uint32_t id; VqeafTaskState state; VqeafTaskKind kind; uint64_t ticks;
 uint64_t rsp;            /* saved kernel/interrupt-stack pointer (pad of trap frame) */
 uint64_t root;           /* address-space root (CR3); kernel tasks use the shared root */
 uint64_t kernel_stack;   /* lowest mapped kernel-stack page (virtual == physical) */
 uint64_t stack_pages;
 uint64_t user_stack;     /* user stack base for USER tasks, else 0 */
 uint64_t user_stack_pages;
 VqeafCred cred;          /* uid/gid/capabilities the dispatcher enforces */
 VqeafTaskFn fn; void *arg;
} VqeafTask;
void vqeaf_sched_init(void);
int vqeaf_sched_add(uint32_t id);            /* metadata-only slot, kept for host unit tests */
void vqeaf_sched_tick(void);                 /* metadata round-robin, kept for host unit tests */
int vqeaf_task_create(VqeafTaskFn fn, void *arg);      /* kernel-mode task */
int vqeaf_task_create_user(uint64_t root,uint64_t entry,uint64_t user_rsp); /* ring-3 task, system creds */
/* Ring-3 task with an explicit credential; NULL cred is rejected so a caller
 * can never forget to assign one and inherit the system identity silently.
 * Returns -6 when the credential is not a valid app credential (system uid or
 * CAP_SYSTEM/CAP_FS_ADMIN present). Boot self-tests use the system path below. */
int vqeaf_task_create_user_cred(uint64_t root,uint64_t entry,uint64_t user_rsp,const VqeafCred *cred);
/* Boot-only EL0 harness path: deliberately grants VQEAF_CRED_SYSTEM_INIT.
 * Never call this for an installed app; AMS uses the credential variant. */
int vqeaf_task_create_user(uint64_t root,uint64_t entry,uint64_t user_rsp);
/* Credential of a live slot, or NULL. The syscall dispatcher reads this. */
const VqeafCred *vqeaf_task_cred_ptr(int slot);
uint64_t vqeaf_task_stack_base(int slot);    /* lowest mapped kernel-stack page, 0 if unused */
void vqeaf_task_exit(void);
void vqeaf_sched_start(void);                /* switch into the first READY task, IRQs on */
uint64_t vqeaf_sched_preempt(uint64_t frame_base);  /* called from the IRQ0 assembly stub */
uint64_t vqeaf_pf_preempt(uint64_t frame_base);     /* called from the page-fault stub */
uint64_t vqeaf_syscall_preempt(uint64_t frame_base);/* called from the syscall stub */
void vqeaf_vfs_release_task(int slot);            /* forward decl for task exit */
void vqeaf_pf_expect(uint64_t virtual_address, uint64_t rip_advance); /* arm guard-page recovery */
uint64_t vqeaf_sched_trap_stack(void);       /* asm helper: stack top for frame relocation */
void vqeaf_sched_set_kernel_root(uint64_t root);
int32_t vqeaf_sched_current(void);
int vqeaf_task_state(int slot);             /* VqeafTaskState of slot, -1 if invalid (AArch64) */
uint64_t vqeaf_timer_ticks(void);
#endif
