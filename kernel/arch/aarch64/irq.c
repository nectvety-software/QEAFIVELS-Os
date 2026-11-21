/* VQEAF AArch64 interrupt support: GICv2 + ARM generic timer + PL011 RX +
 * C exception handlers. QEMU virt places the GIC distributor at 0x08000000,
 * the CPU interface at 0x08010000; the non-secure physical timer is PPI 30
 * and the PL011 UART is SPI 1 (INTID 33, verified from the machine DTB).
 *
 * The timer IRQ drives the scheduler. The UART RX IRQ is the InputFlinger-like
 * entry point: bytes from the host console are dispatched to the focused
 * window, never to every task. EL0 SVC syscalls are decoded from the sync
 * path; every user pointer is validated before the kernel dereferences it.
 */
#include <stdint.h>
#include "vqeaf/uart_aarch64.h"
#include "vqeaf/arch_aarch64.h"
#include "vqeaf/scheduler.h"
#include "vqeaf/vm.h"
#include "vqeaf/compositor.h"
#include "vqeaf/window.h"
#include "vqeaf/ams.h"
#include "vqeaf/security.h"
#include "vqeaf/vfs.h"

#define GICD_BASE 0x08000000ull
#define GICC_BASE 0x08010000ull
#define GICD_CTLR        (*(volatile uint32_t *)(GICD_BASE + 0x000u))
#define GICD_ISENABLER0  (*(volatile uint32_t *)(GICD_BASE + 0x100u))
#define GICD_ISENABLER1  (*(volatile uint32_t *)(GICD_BASE + 0x104u))
#define GICD_IPRIORITYR(n) (*(volatile uint32_t *)(GICD_BASE + 0x400u + 4u * (n)))
#define GICD_ITARGETSR(n)  (*(volatile uint32_t *)(GICD_BASE + 0x800u + 4u * (n)))
#define GICC_CTLR        (*(volatile uint32_t *)(GICC_BASE + 0x000u))
#define GICC_PMR         (*(volatile uint32_t *)(GICC_BASE + 0x004u))
#define GICC_IAR         (*(volatile uint32_t *)(GICC_BASE + 0x00Cu))
#define GICC_EOIR        (*(volatile uint32_t *)(GICC_BASE + 0x010u))

#define TIMER_IRQ 30u

static volatile uint64_t ticks;
static uint64_t timer_freq;
static uint64_t timer_reload;
static volatile uint32_t input_irq_count;

/* Minimal Binder-like kernel IPC channel: one buffered mailbox shared by EL0
 * tasks. sys_send copies a validated user buffer in; sys_recv copies it out to
 * another task's validated user buffer. The kernel never dereferences a user
 * pointer without checking it is user-region memory mapped EL0-accessible. */
#define IPC_BUF_SIZE 256u
static uint8_t ipc_buf[IPC_BUF_SIZE];
static uint64_t ipc_len;
static int ipc_pending;

/* Base for EL0-initiated AMS launches (sys_launch). Distinct from the kernel
 * Launcher's own 0x...400000 and from the static F2a bases. */
static uint64_t launch_base = VQEAF_USER_REGION_BASE + 0x600000ull;

/* Single-CPU critical section. QEMU virt has one core, so masking IRQ is
 * sufficient mutual exclusion against timer-IRQ preemption. Only the I bit
 * is touched; D/A/F are left exactly as they were. */
uint64_t vqeaf_a64_irq_save(void)
{
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    __asm__ volatile("msr daifset, #2" ::: "memory");
    return daif;
}

void vqeaf_a64_irq_restore(uint64_t daif)
{
    /* DAIF.I is bit 7. daifset/daifclr #2 only change I, so restoring I
     * alone is exact and cannot accidentally unmask FIQ/SError/debug. */
    if(daif & (1ull << 7))
        __asm__ volatile("msr daifset, #2" ::: "memory");
    else
        __asm__ volatile("msr daifclr, #2" ::: "memory");
}

static inline uint64_t rd_esr(void) { uint64_t v; __asm__ volatile("mrs %0, esr_el1" : "=r"(v)); return v; }
static inline uint64_t rd_far(void) { uint64_t v; __asm__ volatile("mrs %0, far_el1" : "=r"(v)); return v; }
static inline uint64_t rd_elr(void) { uint64_t v; __asm__ volatile("mrs %0, elr_el1" : "=r"(v)); return v; }

extern void vqeaf_a64_vector_table(void);

void vqeaf_a64_exceptions_init(void)
{
    uint64_t v = (uint64_t)vqeaf_a64_vector_table;
    __asm__ volatile("msr vbar_el1, %0" :: "r"(v) : "memory");
    __asm__ volatile("isb" ::: "memory");
}

void vqeaf_a64_gic_init(void)
{
    GICD_CTLR = 0;
    uint32_t pr = GICD_IPRIORITYR(7);
    pr &= ~(0xFFu << 16);              /* INTID 30 priority byte = 0 (highest) */
    GICD_IPRIORITYR(7) = pr;
    GICD_ISENABLER0 = (1u << TIMER_IRQ);

    /* SPI 1 (INTID 33, PL011 RX): route to CPU0, priority below the timer. */
    uint32_t p8 = GICD_IPRIORITYR(8);
    p8 &= ~(0xFFu << 8);
    p8 |= (0x80u << 8);
    GICD_IPRIORITYR(8) = p8;
    uint32_t t8 = GICD_ITARGETSR(8);
    t8 &= ~(0xFFu << 8);
    t8 |= (0x01u << 8);
    GICD_ITARGETSR(8) = t8;
    GICD_ISENABLER1 = (1u << 1);

    GICD_CTLR = 1;
    GICC_PMR = 0xFFu;
    GICC_CTLR = 1;
}

void vqeaf_a64_input_init(void)
{
    vqeaf_uart_rx_irq_enable();
    vqeaf_uart_write("INPUT: PL011 RX IRQ armed (GIC INTID ");
    vqeaf_uart_u64(VQEAF_A64_UART_IRQ);
    vqeaf_uart_write(")\r\n");
}

uint32_t vqeaf_a64_input_irq_count(void) { return input_irq_count; }

void vqeaf_a64_timer_init(void)
{
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));
    if(!timer_freq) timer_freq = 62500000ull;
    timer_reload = timer_freq / 100ull;  /* ~10 ms per tick */
    if(!timer_reload) timer_reload = 1;
    ticks = 0;
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(timer_reload) : "memory");
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(1ull) : "memory");
    __asm__ volatile("isb" ::: "memory");
}

uint64_t vqeaf_a64_timer_ticks(void) { return ticks; }

uint64_t vqeaf_a64_irq_preempt(uint64_t frame_base)
{
    uint32_t iar = GICC_IAR;
    uint32_t id = iar & 0x3FFu;
    uint64_t resume = frame_base;
    if(id == TIMER_IRQ) {
        ticks++;
        if(ticks <= 3) {
            vqeaf_uart_write("TIMER: tick ");
            vqeaf_uart_u64(ticks);
            vqeaf_uart_write("\r\n");
        }
        __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(timer_reload) : "memory");
        if(vqeaf_a64_sched_active())
            resume = vqeaf_a64_sched_switch(frame_base);
    } else if(id == VQEAF_A64_UART_IRQ) {
        /* InputFlinger: drain the whole FIFO, route each byte by focus. No
         * UART output here: printing from an IRQ would interleave markers. */
        if(vqeaf_uart_rx_irq_pending()) {
            while(vqeaf_uart_rx_available())
                vqeaf_input_push(vqeaf_uart_rx_read());
            input_irq_count++;
        }
        vqeaf_uart_rx_irq_ack();
    }
    GICC_EOIR = iar;
    return resume;
}

/* Copy a bounded NUL-terminated user string one byte at a time, validating
 * each byte is EL0-accessible. Returns 0 on success. */
static int copy_user_cstr(uint64_t root, uint64_t ptr, char *dst, uint64_t cap)
{
    for(uint64_t i = 0; i + 1 < cap; i++) {
        if(!vqeaf_vm_user_range_mapped(root, ptr + i, 1)) return -2;
        char c = (char)((volatile uint8_t *)ptr)[i];
        dst[i] = c;
        if(c == 0) return 0;
    }
    dst[cap - 1] = 0;
    return -3;
}

void vqeaf_a64_sync_handler(uint64_t frame_base)
{
    uint64_t *f = (uint64_t *)frame_base;
    uint64_t esr = rd_esr();
    uint64_t ec = (esr >> 26) & 0x3Fu;
    if(ec == 0x15u) {
        uint64_t origin_el = f[32] & 0x1Fu;  /* SPSR.M[4:0] */
        if(origin_el == 0u) {                /* SVC from EL0t: dispatch syscall */
            uint64_t num = f[8];             /* x8 */
            uint64_t a0 = f[0];              /* x0 */
            uint64_t root = vqeaf_vm_current_root();
            int32_t owner = vqeaf_sched_current();
            uint64_t ret = 0;
            /* Privilege gate: every EL0 syscall is checked against the calling
             * task's credential BEFORE its arguments are inspected. If the
             * caller has no credential at all, the answer is DENY — falling
             * back to a system credential here would turn a stale/exited slot
             * into an unconditional CAP_ALL escalation. */
            const VqeafCred *cred = vqeaf_task_cred_ptr(owner);
            if(!cred) {
                vqeaf_uart_write("SEC: deny no-cred sys=");
                vqeaf_uart_u64(num);
                vqeaf_uart_write("\r\n");
                f[0] = (uint64_t)VQEAF_EACCES;
                return;
            }
            uint64_t need = vqeaf_sec_syscall_cap(num);
            if(need && !vqeaf_sec_has(cred, need)) {
                vqeaf_uart_write("SEC: deny uid=");
                vqeaf_uart_u64(cred->uid);
                vqeaf_uart_write(" sys=");
                vqeaf_uart_u64(num);
                vqeaf_uart_write(" need=");
                vqeaf_uart_hex64(need);
                vqeaf_uart_write("\r\n");
                f[0] = (uint64_t)VQEAF_EACCES;
                return;
            }
            switch(num) {
            case VQEAF_SYS_PUTCHAR:
                if(a0 < 128) vqeaf_uart_char((char)a0);
                ret = 0; break;
            case VQEAF_SYS_PRINT: { /* x0=user string VA, x1=len (atomic marker) */
                uint64_t ptr = f[0], len = f[1];
                if(len > 256 || !vqeaf_vm_user_range_mapped(root, ptr, len)) {
                    ret = (uint64_t)-2; break;
                }
                for(uint64_t k = 0; k < len; k++)
                    vqeaf_uart_char((char)((volatile uint8_t *)ptr)[k]);
                ret = (uint64_t)len; break;
            }
            case VQEAF_SYS_EXIT:
                vqeaf_task_exit();
                ret = 0; break;
            case VQEAF_SYS_SEND: {
                uint64_t ptr = f[0], len = f[1];
                if(!len || len > IPC_BUF_SIZE ||
                   !vqeaf_vm_user_range_mapped(root, ptr, len)) { ret = (uint64_t)-2; break; }
                for(uint64_t k = 0; k < len; k++) ipc_buf[k] = ((volatile uint8_t *)ptr)[k];
                ipc_len = len; ipc_pending = 1;
                vqeaf_uart_write("IPC: kernel accepted send\r\n");
                ret = 0; break;
            }
            case VQEAF_SYS_RECV: {
                uint64_t ptr = f[0], maxlen = f[1];
                if(!ipc_pending) { ret = (uint64_t)-1; break; }
                uint64_t n = ipc_len < maxlen ? ipc_len : maxlen;
                if(!n || !vqeaf_vm_user_range_mapped(root, ptr, n)) { ret = (uint64_t)-2; break; }
                for(uint64_t k = 0; k < n; k++) ((volatile uint8_t *)ptr)[k] = ipc_buf[k];
                ipc_pending = 0;
                vqeaf_uart_write("IPC: kernel delivered recv\r\n");
                ret = n; break;
            }
            case VQEAF_SYS_SUBMIT_SURFACE: { /* F2c legacy path */
                uint64_t ptr = f[0], len = f[1];
                if(!len || len > 4096 ||
                   !vqeaf_vm_user_range_mapped(root, ptr, len)) { ret = (uint64_t)-2; break; }
                int sr = vqeaf_compositor_submit((const void *)ptr, len);
                if(sr == 0) vqeaf_uart_write("COMPOSITOR: kernel accepted surface\r\n");
                ret = (uint64_t)sr; break;
            }
            case VQEAF_SYS_POLL_INPUT: { /* x0=win id, x1=event VA */
                uint64_t ptr = f[1];
                if(!vqeaf_vm_user_range_mapped(root, ptr, VQEAF_KEY_EVENT_SIZE)) {
                    ret = (uint64_t)-2; break;
                }
                vqeaf_key_event_t ev;
                for(unsigned k = 0; k < VQEAF_KEY_EVENT_SIZE; k++)
                    ((volatile uint8_t *)&ev)[k] = 0;
                int r = vqeaf_wm_poll_input((uint32_t)a0, owner, &ev);
                if(r == 1)
                    for(unsigned k = 0; k < VQEAF_KEY_EVENT_SIZE; k++)
                        ((volatile uint8_t *)ptr)[k] = ((volatile uint8_t *)&ev)[k];
                ret = (uint64_t)(int64_t)r; break;
            }
            case VQEAF_SYS_WINDOW_CREATE: { /* x0=desc VA, x1=len */
                uint64_t ptr = f[0], len = f[1];
                if(len != VQEAF_WINDOW_DESC_SIZE ||
                   !vqeaf_vm_user_range_mapped(root, ptr, len)) {
                    ret = (uint64_t)-2; break;
                }
                vqeaf_window_desc_t desc;
                for(unsigned k = 0; k < VQEAF_WINDOW_DESC_SIZE; k++)
                    ((volatile uint8_t *)&desc)[k] = ((volatile uint8_t *)ptr)[k];
                ret = (uint64_t)(int64_t)vqeaf_wm_create(&desc, owner); break;
            }
            case VQEAF_SYS_WINDOW_PRESENT:
                ret = (uint64_t)(int64_t)vqeaf_wm_present((uint32_t)a0, owner); break;
            case VQEAF_SYS_WINDOW_SET_Z:
                ret = (uint64_t)(int64_t)vqeaf_wm_set_z((uint32_t)a0, owner,
                                                        (int32_t)f[1]); break;
            case VQEAF_SYS_WINDOW_CLOSE:
                ret = (uint64_t)(int64_t)vqeaf_wm_close((uint32_t)a0, owner); break;
            case VQEAF_SYS_LAUNCH: { /* x0=user package name VA */
                char name[48];
                if(copy_user_cstr(root, a0, name, sizeof(name)) != 0) {
                    ret = (uint64_t)-2; break;
                }
                uint64_t base = launch_base;
                launch_base += 0x200000ull;
                int pid = vqeaf_ams_launch(name, base);
                vqeaf_uart_write("SYSCALL: EL0 launch request ");
                vqeaf_uart_write(name);
                vqeaf_uart_write(" -> pid=");
                vqeaf_uart_u64((uint64_t)(int64_t)pid);
                vqeaf_uart_write("\r\n");
                ret = (uint64_t)(int64_t)(pid > 0 ? pid : -1); break;
            }
            case VQEAF_SYS_FRAME_DONE:
                ret = (uint64_t)vqeaf_wm_frame_done(); break;
            case VQEAF_SYS_OPEN: { /* x0=path VA, x1=open flags */
                char path[64];
                uint64_t flags = f[1];
                if(copy_user_cstr(root, a0, path, sizeof(path)) != 0) {
                    ret = (uint64_t)VQEAF_EINVAL; break;
                }
                /* Read access is the baseline capability; creating a writable
                 * handle needs the separate write capability. */
                if((flags & VQEAF_VFS_OPEN_WRITE) &&
                   !vqeaf_sec_has(cred, VQEAF_CAP_FS_WRITE)) {
                    vqeaf_uart_write("SEC: deny uid=");
                    vqeaf_uart_u64(cred->uid);
                    vqeaf_uart_write(" sys=");
                    vqeaf_uart_u64(num);
                    vqeaf_uart_write(" need=");
                    vqeaf_uart_hex64(VQEAF_CAP_FS_WRITE);
                    vqeaf_uart_write("\r\n");
                    ret = (uint64_t)VQEAF_EACCES; break;
                }
                ret = (uint64_t)(int64_t)vqeaf_vfs_handle_open(
                          owner, cred, path, (uint32_t)flags);
                break;
            }
            case VQEAF_SYS_READ: { /* x0=handle, x1=buf VA, x2=len */
                static uint8_t xfer[256];
                uint64_t ptr = f[1], len = f[2];
                if(!len || len > sizeof(xfer) ||
                   !vqeaf_vm_user_range_mapped(root, ptr, len)) {
                    ret = (uint64_t)VQEAF_EINVAL; break;
                }
                int64_t n = vqeaf_vfs_handle_read(owner, cred, (int)a0,
                                                  xfer, len);
                if(n > 0)
                    for(int64_t k = 0; k < n; k++)
                        ((volatile uint8_t *)ptr)[k] = xfer[k];
                ret = (uint64_t)n; break;
            }
            case VQEAF_SYS_WRITE: { /* x0=handle, x1=buf VA, x2=len */
                static uint8_t xfer[256];
                uint64_t ptr = f[1], len = f[2];
                if(!len || len > sizeof(xfer) ||
                   !vqeaf_vm_user_range_mapped(root, ptr, len)) {
                    ret = (uint64_t)VQEAF_EINVAL; break;
                }
                for(uint64_t k = 0; k < len; k++)
                    xfer[k] = ((volatile uint8_t *)ptr)[k];
                ret = (uint64_t)vqeaf_vfs_handle_write(owner, cred, (int)a0,
                                                       xfer, len);
                break;
            }
            case VQEAF_SYS_CLOSE:
                ret = (uint64_t)(int64_t)vqeaf_vfs_handle_close(owner, (int)a0);
                break;
            default:
                ret = (uint64_t)-2; break;
            }
            f[0] = ret;
            return;
        }
        /* SVC from EL1 (controlled self-test): ELR already points past it. */
        vqeaf_uart_write("EXC: SVC decoded (EC=0x15)\r\n");
        return;
    }
    vqeaf_uart_write("EXC: unexpected sync EC=0x");
    vqeaf_uart_hex64(ec);
    vqeaf_uart_write(" ESR=0x");
    vqeaf_uart_hex64(esr);
    vqeaf_uart_write(" FAR=0x");
    vqeaf_uart_hex64(rd_far());
    vqeaf_uart_write(" ELR=0x");
    vqeaf_uart_hex64(rd_elr());
    vqeaf_uart_write("\r\n");
    for(;;) __asm__ volatile("wfi");
}

void vqeaf_a64_bad_handler(void)
{
    vqeaf_uart_write("EXC: unexpected vector\r\n");
    for(;;) __asm__ volatile("wfi");
}
