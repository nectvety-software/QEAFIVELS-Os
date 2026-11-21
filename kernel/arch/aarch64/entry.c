/* VQEAF AArch64 kernel early entry (Milestones A + B: boot + MMU).
 *
 * Runs at EL1 on QEMU virt. Brings up the PL011 UART, the physical page
 * allocator, an identity map of RAM + UART, then enables the MMU and proves
 * both identity and non-identity translation. Later milestones add exceptions,
 * scheduler, EL0 tasks, ELF loading and IPC. EMULATED evidence, not violet
 * hardware. */
#include <stdint.h>
#include "vqeaf/uart_aarch64.h"
#include "vqeaf/memory.h"
#include "vqeaf/vm.h"
#include "vqeaf/arch_aarch64.h"
#include "vqeaf/virtio_mmio_blk.h"

extern char _stack_top[];

static inline uint64_t rd_currentel(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return (v >> 2) & 3u;
}

static inline uint64_t rd_mpidr(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(v));
    return v;
}

static inline uint64_t rd_sctlr(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v));
    return v;
}

static volatile uint64_t ram_probe;

/* EFI-style descriptor consumed by the portable PMM (type 7 = conventional). */
typedef struct {
    uint32_t type, pad;
    uint64_t physical_start, virtual_start, number_of_pages, attribute;
} EfiDesc;

/* ---- Milestone D: preemptive scheduler + EL0 user task ---- */
extern char _user_stub_start[], _user_stub_end[];
#include "vqeaf/scheduler.h"
#include "vqeaf/elf.h"
#include "vqeaf/qen.h"
#include "vqeaf/ams.h"
#include "vqeaf/display_aarch64.h"
#include "vqeaf/compositor.h"
#include "vqeaf/window.h"
#include "vqeaf/security.h"
#include "vqeaf/vfs.h"
#include "../../../user/hello_aarch64_elf.h"
#include "../../../user/ipc_send_aarch64_elf.h"
#include "../../../user/ipc_recv_aarch64_elf.h"
#include "../../../user/qen_app_aarch64_qen.h"
#include "../../../user/surface_client_aarch64_elf.h"
#include "../../../user/launcher_aarch64_elf.h"

static void busy_delay(volatile uint64_t n)
{
    while(n--) __asm__ volatile("nop");
}

static void task_t1(void *arg)
{
    (void)arg;
    for(int i = 0; i < 3; i++) {
        vqeaf_uart_write("SCHED: T1 alive\r\n");
        busy_delay(2000000);
    }
}

static void task_t2(void *arg)
{
    (void)arg;
    for(int i = 0; i < 3; i++) {
        vqeaf_uart_write("SCHED: T2 alive\r\n");
        busy_delay(2000000);
    }
}

/* ---- Milestone F2b: Launcher + ActivityManagerService ----
 * A kernel task plays the Launcher/AMS role: it lists the installed
 * .qen apps, asks AMS to spawn one as an isolated EL0 process (the Zygote-fork
 * equivalent: fresh user root + task_create_user), then waits for that process
 * to exit and records the lifecycle event. It stays alive until the app is done
 * so the scheduler does not power off before the launched app has run. */
static void task_ams(void *arg)
{
    (void)arg;
    vqeaf_uart_write("LAUNCHER: home ready\r\n");
    int n = vqeaf_ams_app_count();
    for(int i = 0; i < n; i++) {
        vqeaf_uart_write("LAUNCHER: installed ");
        vqeaf_uart_write(vqeaf_ams_app_name(i));
        vqeaf_uart_write("\r\n");
    }
    const uint64_t app_base = VQEAF_USER_REGION_BASE + 0x400000ull;
    int pid = vqeaf_ams_launch("com.vqeaf.demo", app_base);
    if(pid < 0) {
        vqeaf_uart_write("LAUNCHER: launch FAILED r=");
        vqeaf_uart_u64((uint64_t)(-pid));
        vqeaf_uart_write("\r\n");
        return;
    }
    vqeaf_uart_write("LAUNCHER: spawned app pid=");
    vqeaf_uart_u64((uint64_t)pid);
    vqeaf_uart_write("\r\n");
    int slot = vqeaf_ams_process_slot(pid);
    for(int i = 0; i < 2000; i++) {
        if(vqeaf_task_state(slot) == VQEAF_TASK_EXITED) break;
        busy_delay(100000); /* timer IRQ preempts us so the app can run */
    }
    vqeaf_uart_write("AMS: process pid=");
    vqeaf_uart_u64((uint64_t)pid);
    if(vqeaf_task_state(slot) == VQEAF_TASK_EXITED)
        vqeaf_uart_write(" exited\r\n");
    else
        vqeaf_uart_write(" still running (timeout)\r\n");
}

/* ---- F4: credential + VFS self-test ---------------------------------------
 * Runs before the scheduler is live, against the same functions the EL0
 * dispatcher calls. Every line is real output from the real policy table and
 * the real handle table, so the QEMU gate can assert the model end to end.
 * Not a substitute for an EL0 denial test; the dispatcher path is wired above
 * and apps launched by AMS run with the restricted credential. */
#define VQEAF_STR_(x) #x
#define VQEAF_STR(x) VQEAF_STR_(x)
#define SELFTEST_APP1_PATH "/data/" VQEAF_STR(VQEAF_UID_FIRST_APP) "/private"
#define SELFTEST_APP2_PATH "/data/" VQEAF_STR(VQEAF_UID_SECOND_APP) "/private"

static void sec_expect(const VqeafCred *c, uint64_t nr, int want_allow)
{
    int ok = vqeaf_sec_allows(c, nr) ? 1 : 0;
    vqeaf_uart_write(ok == want_allow ? "SEC: " : "SEC: FAILED ");
    vqeaf_uart_write(want_allow ? "allow uid=" : "deny uid=");
    vqeaf_uart_u64(c->uid);
    vqeaf_uart_write(" sys=");
    vqeaf_uart_u64(nr);
    vqeaf_uart_write(" need=");
    vqeaf_uart_hex64(vqeaf_sec_syscall_cap(nr));
    vqeaf_uart_write("\r\n");
}

static void vfs_step(const char *what, int64_t r)
{
    vqeaf_uart_write(what);
    if(r >= 0) { vqeaf_uart_write(" -> h="); vqeaf_uart_u64((uint64_t)r); }
    else if(r == VQEAF_EACCES) vqeaf_uart_write(" -> EACCES");
    else if(r == VQEAF_ENOENT) vqeaf_uart_write(" -> ENOENT");
    else { vqeaf_uart_write(" -> err="); vqeaf_uart_u64((uint64_t)(-r)); }
    vqeaf_uart_write("\r\n");
}

static void security_selftest(void)
{
    static const VqeafCred system_cred = VQEAF_CRED_SYSTEM_INIT;
    VqeafCred app1 = { VQEAF_UID_FIRST_APP, VQEAF_GID_APPS, VQEAF_CAPS_APP_HEADLESS };
    VqeafCred app2 = { VQEAF_UID_SECOND_APP, VQEAF_GID_APPS, VQEAF_CAPS_APP_HEADLESS };
    char buf[8];

    vqeaf_uart_write("SEC: credential model online (uid/gid/caps per task)\r\n");
    vqeaf_uart_write("SEC: kernel cred uid=0 caps=");
    vqeaf_uart_hex64(system_cred.caps);
    vqeaf_uart_write("\r\n");
    sec_expect(&system_cred, VQEAF_SYS_WINDOW_CREATE, 1);
    sec_expect(&app2, VQEAF_SYS_WINDOW_CREATE, 0);
    sec_expect(&app2, VQEAF_SYS_SUBMIT_SURFACE, 0);
    sec_expect(&app2, VQEAF_SYS_PRINT, 1);
    sec_expect(&app2, VQEAF_SYS_WRITE, 0);

    /* A user task may never be created with kernel-only authority, and an
     * unknown slot must have NO credential at all (the SVC path fails closed
     * on NULL instead of falling back to CAP_ALL). */
    {
        VqeafCred as_system = { VQEAF_UID_SYSTEM, VQEAF_GID_APPS,
                                VQEAF_CAPS_APP_UI };
        VqeafCred as_admin = { VQEAF_UID_FIRST_APP, VQEAF_GID_APPS,
                               VQEAF_CAPS_APP_UI | VQEAF_CAP_FS_ADMIN };
        vqeaf_uart_write(vqeaf_sec_user_cred_ok(&as_system) == 0
                         ? "SEC: system uid rejected for user task\r\n"
                         : "SEC: FAILED system uid accepted for user task\r\n");
        vqeaf_uart_write(vqeaf_sec_user_cred_ok(&as_admin) == 0
                         ? "SEC: FS_ADMIN capability rejected for user task\r\n"
                         : "SEC: FAILED FS_ADMIN accepted for user task\r\n");
        vqeaf_uart_write(vqeaf_task_cred_ptr(-1) == 0
                         ? "SEC: unknown slot has no credential (fail closed)\r\n"
                         : "SEC: FAILED unknown slot exposes a credential\r\n");
        /* root=0 would return -1 from the creator body; -6 proves the policy
         * check rejected the credential BEFORE any task state was touched. */
        int refused = vqeaf_task_create_user_cred(0, 0, 0, &as_system);
        vqeaf_uart_write("SEC: user task create with system cred -> ");
        if(refused == -6) vqeaf_uart_write("refused -6\r\n");
        else { vqeaf_uart_write("err="); vqeaf_uart_u64((uint64_t)refused);
               vqeaf_uart_write("\r\n"); }
    }

    vqeaf_uart_write("VFS: ramfs root online (/system/version 0444, /data/<uid>/private 0600)\r\n");
    int h = vqeaf_vfs_handle_open(0, &app1, SELFTEST_APP1_PATH, VQEAF_O_RDWR);
    vfs_step("VFS: uid=10001 open own private", h);
    if(h >= 0) {
        int64_t w = vqeaf_vfs_handle_write(0, &app1, h, "hello", 5);
        vqeaf_uart_write("VFS: uid=10001 write 5 bytes -> ");
        vqeaf_uart_u64((uint64_t)w);
        vqeaf_uart_write("\r\n");
        vqeaf_vfs_handle_close(0, h);
        int rh = vqeaf_vfs_handle_open(0, &app1, SELFTEST_APP1_PATH, VQEAF_O_RDONLY);
        int64_t r = 0;
        if(rh >= 0) {
            r = vqeaf_vfs_handle_read(0, &app1, rh, buf, 5);
            vqeaf_vfs_handle_close(0, rh);
        }
        vqeaf_uart_write("VFS: uid=10001 read 5 bytes -> ");
        vqeaf_uart_u64((uint64_t)r);
        vqeaf_uart_write(" bytes\r\n");
    }
    int cross = vqeaf_vfs_handle_open(1, &app2, SELFTEST_APP1_PATH, VQEAF_O_RDONLY);
    vfs_step("VFS: uid=10002 open /data/10001/private", cross);

    int vh = vqeaf_vfs_handle_open(1, &app1, "/system/version", VQEAF_O_RDONLY);
    int64_t vn = 0;
    if(vh >= 0) {
        vn = vqeaf_vfs_handle_read(1, &app1, vh, buf, sizeof(buf));
        vqeaf_vfs_handle_close(1, vh);
    }
    vqeaf_uart_write("VFS: uid=10001 read /system/version -> ");
    vqeaf_uart_u64((uint64_t)vn);
    vqeaf_uart_write(" bytes\r\n");
    int vw = vqeaf_vfs_handle_open(1, &app1, "/system/version", VQEAF_O_WRONLY);
    vfs_step("VFS: uid=10001 write /system/version", vw);

    /* Two slots must never share handle numbers or file state. */
    int ha = vqeaf_vfs_handle_open(0, &app1, SELFTEST_APP1_PATH, VQEAF_O_RDONLY);
    int hb = vqeaf_vfs_handle_open(1, &app2, SELFTEST_APP2_PATH, VQEAF_O_RDONLY);
    vqeaf_uart_write("VFS: per-task handle tables isolated slot0 h=");
    vqeaf_uart_u64((uint64_t)ha);
    vqeaf_uart_write(" slot1 h=");
    vqeaf_uart_u64((uint64_t)hb);
    vqeaf_uart_write("\r\n");

    /* A handle is bound to the identity that opened it. Reaching the same
     * slot and handle number with another app's credential must fail. */
    int bound = vqeaf_vfs_handle_open(2, &app1, SELFTEST_APP1_PATH, VQEAF_O_RDONLY);
    if(bound >= 0) {
        int64_t foreign = vqeaf_vfs_handle_read(2, &app2, bound, buf, 1);
        vqeaf_uart_write("VFS: foreign cred on bound handle -> ");
        if(foreign == VQEAF_EACCES) vqeaf_uart_write("EACCES");
        else { vqeaf_uart_write("err="); vqeaf_uart_u64((uint64_t)(-foreign)); }
        vqeaf_uart_write("\r\n");
        vqeaf_vfs_handle_close(2, bound);
    }

    vqeaf_vfs_release_task(0);
    vqeaf_vfs_release_task(1);
    int again = vqeaf_vfs_handle_open(1, &app2, SELFTEST_APP2_PATH, VQEAF_O_RDONLY);
    vqeaf_uart_write("VFS: task exit released slot handles -> h=");
    vqeaf_uart_u64((uint64_t)again);
    vqeaf_uart_write("\r\n");
    vqeaf_vfs_release_task(1);
}

void kernel_main(void)
{
    vqeaf_uart_init();
    vqeaf_uart_write("VQEAF kernel aarch64 0.1: native entry reached\r\n");

    uint64_t el = rd_currentel();
    vqeaf_uart_write("AARCH64: CurrentEL=EL");
    vqeaf_uart_char((char)('0' + el));
    vqeaf_uart_write("\r\n");

    vqeaf_uart_write("AARCH64: MPIDR=");
    vqeaf_uart_hex64(rd_mpidr());
    vqeaf_uart_write("\r\n");

    ram_probe = 0xDEADBEEFCAFEBABEull;
    if(ram_probe == 0xDEADBEEFCAFEBABEull)
        vqeaf_uart_write("AARCH64: RAM probe write/read OK\r\n");
    else
        vqeaf_uart_write("AARCH64: RAM probe FAILED\r\n");

    /* Physical memory: QEMU virt RAM above the kernel image + stack. */
    uint64_t kern_end = ((uint64_t)_stack_top + VQEAF_PAGE_SIZE - 1) & ~(VQEAF_PAGE_SIZE - 1);
    uint64_t ram_end = VQEAF_A64_RAM_BASE + VQEAF_A64_RAM_SIZE;
    EfiDesc map[1];
    map[0].type = 7u; map[0].pad = 0;
    map[0].physical_start = kern_end; map[0].virtual_start = kern_end;
    map[0].number_of_pages = (ram_end - kern_end) / VQEAF_PAGE_SIZE;
    map[0].attribute = 0;
    if(vqeaf_pmm_init(map, sizeof(map), sizeof(EfiDesc)) != 0) {
        vqeaf_uart_write("PMM: init FAILED\r\n");
        return;
    }
    vqeaf_uart_write("PMM: free pages=");
    vqeaf_uart_u64(vqeaf_pmm_free_count());
    vqeaf_uart_write("\r\n");

    uint64_t root = vqeaf_vm_create_root();
    if(!root) { vqeaf_uart_write("VM: root alloc FAILED\r\n"); return; }
    if(vqeaf_vm_map_range(root, VQEAF_A64_RAM_BASE, VQEAF_A64_RAM_BASE,
                          VQEAF_A64_RAM_SIZE / VQEAF_PAGE_SIZE, VQEAF_VM_WRITE) != 0) {
        vqeaf_uart_write("VM: RAM identity map FAILED\r\n");
        return;
    }
    if(vqeaf_vm_map_range(root, VQEAF_A64_UART_BASE, VQEAF_A64_UART_BASE, 1,
                          VQEAF_VM_WRITE | VQEAF_VM_CACHE_DISABLE) != 0) {
        vqeaf_uart_write("VM: UART map FAILED\r\n");
        return;
    }
    /* GICv2 distributor + CPU interface (0x08000000..0x08020000) as device. */
    if(vqeaf_vm_map_range(root, 0x08000000ull, 0x08000000ull, 32,
                          VQEAF_VM_WRITE | VQEAF_VM_CACHE_DISABLE) != 0) {
        vqeaf_uart_write("VM: GIC map FAILED\r\n");
        return;
    }
    /* fw_cfg MMIO (0x09020000) for the ramfb display path. */
    if(vqeaf_vm_map_range(root, 0x09020000ull, 0x09020000ull, 1,
                          VQEAF_VM_WRITE | VQEAF_VM_CACHE_DISABLE) != 0) {
        vqeaf_uart_write("VM: fw_cfg map FAILED\r\n");
        return;
    }
    vqeaf_vm_activate(root);
    vqeaf_aarch64_mmu_enable();

    vqeaf_uart_write("MMU: identity page tables active; TTBR0=");
    vqeaf_uart_hex64(vqeaf_vm_current_root());
    vqeaf_uart_write("\r\n");
    if(rd_sctlr() & 1u)
        vqeaf_uart_write("MMU: SCTLR.M=1 confirmed\r\n");
    else
        vqeaf_uart_write("MMU: SCTLR.M=0 FAILED\r\n");
    /* Printing after MMU enable proves the device mapping is correct. */
    vqeaf_uart_write("MMU: post-enable UART OK\r\n");

    /* Non-identity translation probe: map a fresh frame at a distinct VA and
     * confirm writes through the VA are visible through the identity PA. */
    uint64_t probe_pa = vqeaf_pmm_alloc_page();
    if(probe_pa) {
        const uint64_t test_va = 0x20000000000ull;
        if(vqeaf_vm_map(root, test_va, probe_pa, VQEAF_VM_WRITE) == 0) {
            volatile uint64_t *via_va = (volatile uint64_t *)test_va;
            volatile uint64_t *via_pa = (volatile uint64_t *)probe_pa;
            *via_va = 0x1122334455667788ull;
            if(*via_va == 0x1122334455667788ull && *via_pa == 0x1122334455667788ull)
                vqeaf_uart_write("MMU: non-identity VA translation OK\r\n");
            else
                vqeaf_uart_write("MMU: VA translation FAILED\r\n");
            vqeaf_vm_unmap(root, test_va);
        } else {
            vqeaf_uart_write("MMU: probe map FAILED\r\n");
        }
        vqeaf_pmm_free_page(probe_pa);
    } else {
        vqeaf_uart_write("MMU: probe alloc FAILED\r\n");
    }

    /* Exceptions + GICv2 + generic timer: observe 3 ticks, then a controlled
     * SVC from EL1 to prove the sync vector and ESR decode. */
    vqeaf_a64_exceptions_init();
    vqeaf_uart_write("EXC: vectors installed\r\n");
    /* Window manager + focus stack must exist before any IRQ can deliver a
     * key, so initialise it while interrupts are still masked. */
    vqeaf_wm_init();
    vqeaf_a64_gic_init();
    vqeaf_uart_write("GIC: distributor + CPU interface online\r\n");
    vqeaf_a64_input_init();
    vqeaf_a64_timer_init();
    __asm__ volatile("msr daifclr, #2" ::: "memory"); /* unmask IRQ */
    while(vqeaf_a64_timer_ticks() < 3)
        __asm__ volatile("wfi");
    __asm__ volatile("msr daifset, #2" ::: "memory"); /* mask IRQ */
    vqeaf_uart_write("TIMER: 3 ticks observed\r\n");
    __asm__ volatile("svc #0");
    vqeaf_uart_write("EXC: returned from SVC\r\n");

    vqeaf_uart_write("AARCH64 KERNEL: boot OK\r\n");

    /* ---- Milestone F2c: ramfb display bring-up ----
     * Allocate the compositor framebuffer and point QEMU's ramfb at it so the
     * composed image is scanned out. The compositor task fills it later. */
    if(vqeaf_display_init() == 0)
        vqeaf_ramfb_set(vqeaf_display_fb_pa(), VQEAF_FB_WIDTH, VQEAF_FB_HEIGHT,
                        VQEAF_FB_WIDTH * VQEAF_FB_BPP);

    /* ---- Milestone G1: virtio-mmio block driver (v0.2 driver model) ----
     * IRQ is still masked here, so the synchronous scratch-sector self-test
     * runs deterministically before the scheduler starts. READY is published
     * only after a real probe + write/read verification. */
    if(vqeaf_virtio_mmio_blk_init() == 0)
        vqeaf_uart_write("G1: virtio-mmio storage online\r\n");
    else
        vqeaf_uart_write("G1: virtio-mmio storage absent (no block transport)\r\n");

    /* ---- Milestone D: scheduler + EL0 task ----
     * IRQ stays masked here; the first task's eret loads SPSR with I=0, which
     * unmaps IRQ only once the task context is live (no entry-context race). */
    vqeaf_sched_init();
    vqeaf_sched_set_kernel_root(root);
    vqeaf_ams_init(root);
    vqeaf_ramfs_init();
    security_selftest();

    /* Copy the PIC EL0 stub into a fresh frame and map it EL0-accessible in a
     * per-task user root. The copy targets the identity PA (kernel_root does
     * not map the user region); the user root maps it at USER_REGION_BASE. */
    uint64_t ucode = vqeaf_pmm_alloc_page();
    uint64_t uroot = vqeaf_vm_user_root(root);
    if(!ucode || !uroot) { vqeaf_uart_write("SCHED: user setup FAILED\r\n"); return; }
    uint64_t stub_len = (uint64_t)(_user_stub_end - _user_stub_start);
    for(uint64_t i = 0; i < stub_len; i++)
        ((volatile uint8_t *)ucode)[i] = ((volatile uint8_t *)_user_stub_start)[i];
    /* Make the freshly written code visible to instruction fetch (PoU). */
    __asm__ volatile("dsb ishst" ::: "memory");
    for(uint64_t a = ucode; a < ucode + stub_len; a += 64)
        __asm__ volatile("dc cvau, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    for(uint64_t a = ucode; a < ucode + stub_len; a += 64)
        __asm__ volatile("ic ivau, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("isb" ::: "memory");
    if(vqeaf_vm_map(uroot, VQEAF_USER_REGION_BASE, ucode,
                    VQEAF_VM_WRITE | VQEAF_VM_USER) != 0) {
        vqeaf_uart_write("SCHED: user code map FAILED\r\n"); return;
    }

    int t1 = vqeaf_task_create(task_t1, 0);
    int t2 = vqeaf_task_create(task_t2, 0);
    int u1 = vqeaf_task_create_user(uroot, VQEAF_USER_REGION_BASE,
                 VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE);
    if(t1 < 0 || t2 < 0 || u1 < 0) {
        vqeaf_uart_write("SCHED: task create FAILED\r\n"); return;
    }

    /* ---- Milestone E: load a real static AArch64 ELF into a second user root
     * and run it at EL0. Exercises the portable vqeaf_elf_load (EM_AARCH64). ---- */
    uint64_t uroot2 = vqeaf_vm_user_root(root);
    uint64_t entry2 = 0;
    if(!uroot2) { vqeaf_uart_write("ELF: user root alloc FAILED\r\n"); return; }
    int elf_r = vqeaf_elf_load(uroot2, vqeaf_user_hello_aarch64_elf,
                               VQEAF_USER_HELLO_AARCH64_SIZE, &entry2);
    if(elf_r != 0) {
        vqeaf_uart_write("ELF: user program load FAILED r=");
        vqeaf_uart_u64((uint64_t)(-elf_r));
        vqeaf_uart_write("\r\n"); return;
    }
    int u2 = vqeaf_task_create_user(uroot2, entry2,
                 VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE);
    if(u2 < 0) { vqeaf_uart_write("ELF: user task create FAILED\r\n"); return; }
    vqeaf_uart_write("ELF: user program loaded; entry=");
    vqeaf_uart_hex64(entry2);
    vqeaf_uart_write("\r\n");

    /* ---- Milestone F: Binder-like kernel IPC between two isolated EL0 tasks.
     * Each program is loaded into its own user root (separate address space);
     * the sender hands a message to the kernel channel and the receiver pulls
     * it out, proving kernel-mediated inter-process communication. ---- */
    uint64_t sroot = vqeaf_vm_user_root(root);
    uint64_t rroot = vqeaf_vm_user_root(root);
    uint64_t sentry = 0, rentry = 0;
    if(!sroot || !rroot) { vqeaf_uart_write("IPC: user root alloc FAILED\r\n"); return; }
    int sr = vqeaf_elf_load(sroot, vqeaf_user_ipc_send_aarch64_elf,
                            VQEAF_USER_IPC_SEND_AARCH64_SIZE, &sentry);
    int rr = vqeaf_elf_load(rroot, vqeaf_user_ipc_recv_aarch64_elf,
                            VQEAF_USER_IPC_RECV_AARCH64_SIZE, &rentry);
    if(sr != 0 || rr != 0) { vqeaf_uart_write("IPC: program load FAILED\r\n"); return; }
    int u3 = vqeaf_task_create_user(sroot, sentry,
                 VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE);
    int u4 = vqeaf_task_create_user(rroot, rentry,
                 VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE);
    if(u3 < 0 || u4 < 0) { vqeaf_uart_write("IPC: task create FAILED\r\n"); return; }
    vqeaf_uart_write("IPC: sender + receiver EL0 tasks created\r\n");

    /* ---- Milestone F2a: .qen loader + aarch64 relocation ----
     * Load the SAME position-independent .qen module at two different bases in
     * two isolated user roots. The loader applies R_AARCH64_RELATIVE fixups by
     * the load base; both instances must print the correct message, proving
     * the module is genuinely relocated (the "load app code" step). */
    const uint64_t qen_base1 = VQEAF_USER_REGION_BASE;
    const uint64_t qen_base2 = VQEAF_USER_REGION_BASE + 0x200000ull;
    uint64_t qroot1 = vqeaf_vm_user_root(root);
    uint64_t qroot2 = vqeaf_vm_user_root(root);
    uint64_t qentry1 = 0, qentry2 = 0;
    if(!qroot1 || !qroot2) { vqeaf_uart_write("QEN: user root alloc FAILED\r\n"); return; }
    int q1 = vqeaf_qen_load(qroot1, vqeaf_user_qen_app_aarch64_qen,
                            VQEAF_USER_QEN_APP_AARCH64_SIZE, qen_base1, &qentry1);
    int q2 = vqeaf_qen_load(qroot2, vqeaf_user_qen_app_aarch64_qen,
                            VQEAF_USER_QEN_APP_AARCH64_SIZE, qen_base2, &qentry2);
    if(q1 != 0 || q2 != 0) {
        vqeaf_uart_write("QEN: module load FAILED r1=");
        vqeaf_uart_u64((uint64_t)(-q1));
        vqeaf_uart_write(" r2=");
        vqeaf_uart_u64((uint64_t)(-q2));
        vqeaf_uart_write("\r\n"); return;
    }
    int q5 = vqeaf_task_create_user(qroot1, qentry1,
                 VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE);
    int q6 = vqeaf_task_create_user(qroot2, qentry2,
                 VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE);
    if(q5 < 0 || q6 < 0) { vqeaf_uart_write("QEN: task create FAILED\r\n"); return; }
    vqeaf_uart_write("QEN: module loaded at base1=");
    vqeaf_uart_hex64(qen_base1);
    vqeaf_uart_write(" base2=");
    vqeaf_uart_hex64(qen_base2);
    vqeaf_uart_write("\r\n");
    vqeaf_uart_write("QEN: R_AARCH64_RELATIVE applied; two isolated EL0 instances\r\n");

    /* ---- Milestone F2b: Launcher/AMS kernel task ----
     * Spawns a third .qen EL0 process dynamically at run time (after the
     * scheduler is live), proving the launch path. */
    int ams = vqeaf_task_create(task_ams, 0);
    if(ams < 0) { vqeaf_uart_write("AMS: launcher task create FAILED\r\n"); return; }

    /* ---- Milestone F2c: compositor client (EL0) + compositor kernel task ----
     * The client submits two surfaces via sys_submit_surface; the compositor
     * task blits them into the ramfb framebuffer and reads back pixels. */
    vqeaf_compositor_init();
    uint64_t croot = vqeaf_vm_user_root(root);
    uint64_t centry = 0;
    if(!croot) { vqeaf_uart_write("COMPOSITOR: user root alloc FAILED\r\n"); return; }
    int cr = vqeaf_elf_load(croot, vqeaf_user_surface_client_aarch64_elf,
                            VQEAF_USER_SURFACE_CLIENT_AARCH64_SIZE, &centry);
    if(cr != 0) { vqeaf_uart_write("COMPOSITOR: client load FAILED\r\n"); return; }
    int uc = vqeaf_task_create_user(croot, centry,
                 VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE);
    int comp = vqeaf_task_create(vqeaf_compositor_task, 0);
    if(uc < 0 || comp < 0) { vqeaf_uart_write("COMPOSITOR: task create FAILED\r\n"); return; }
    vqeaf_uart_write("COMPOSITOR: client + compositor tasks created\r\n");

    /* ---- F3: SystemUI-like launcher runs at EL0 -------------------------
     * Unlike the F2b kernel-task launcher, this one is a real unprivileged
     * process. It creates a window through the surface path, draws into its
     * own mapped buffer, receives keyboard events through the input path and
     * asks AMS to spawn an app with sys_launch. That closes the launch-path
     * chain InputFlinger -> SystemUI -> AMS -> EL0 app -> SurfaceFlinger. */
    uint64_t lroot = vqeaf_vm_user_root(root);
    uint64_t lentry = 0;
    if(!lroot) { vqeaf_uart_write("LAUNCHER EL0: user root alloc FAILED\r\n"); return; }
    int lr = vqeaf_elf_load(lroot, vqeaf_user_launcher_aarch64_elf,
                            VQEAF_USER_LAUNCHER_AARCH64_SIZE, &lentry);
    if(lr != 0) {
        vqeaf_uart_write("LAUNCHER EL0: program load FAILED r=");
        vqeaf_uart_u64((uint64_t)(-lr));
        vqeaf_uart_write("\r\n"); return;
    }
    int ul = vqeaf_task_create_user(lroot, lentry,
                 VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE);
    if(ul < 0) { vqeaf_uart_write("LAUNCHER EL0: task create FAILED\r\n"); return; }
    vqeaf_uart_write("LAUNCHER EL0: SystemUI process created\r\n");

    vqeaf_uart_write("SCHED: tasks created (kernel=4, user=8)\r\n");
    vqeaf_uart_write("SCHED: starting scheduler\r\n");
    vqeaf_sched_start(); /* never returns: erets into the first task */
    for(;;) __asm__ volatile("wfi");
}