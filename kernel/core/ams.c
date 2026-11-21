#include "../include/vqeaf/ams.h"
#include "../include/vqeaf/qen.h"
#include "../include/vqeaf/security.h"
#include "../include/vqeaf/vm.h"
#include "../include/vqeaf/memory.h"
#include "../include/vqeaf/scheduler.h"
#include "../include/vqeaf/uart_aarch64.h"
#include "../include/vqeaf/arch_aarch64.h"
#include "../../user/qen_app_aarch64_qen.h"
#include "../../user/qen_window_aarch64_qen.h"
/* ActivityManagerService-like app process manager (independent kernel).
 * Loads a registered .qen app into a fresh isolated user root and spawns it as
 * an EL0 process. This is the launch path: Launcher ->
 * AMS -> (Zygote fork equivalent) -> app process with its own address space. */
/* Each installed app owns an identity and a capability bundle. AMS is the only
 * code that assigns them; nothing an EL0 process passes can change its uid or
 * widen its mask, and the dispatcher enforces the mask on every syscall. */
typedef struct { const char *name; const unsigned char *image; uint64_t size;
                 uint32_t uid; uint64_t caps; } AmsApp;
static const AmsApp apps[] = {
    {"com.vqeaf.demo", vqeaf_user_qen_app_aarch64_qen, VQEAF_USER_QEN_APP_AARCH64_SIZE,
     VQEAF_UID_FIRST_APP, VQEAF_CAPS_APP_HEADLESS},
    /* F3: an app that opens its own window via the surface path when spawned
     * by the EL0 launcher (sys_launch). */
    {"com.vqeaf.window", vqeaf_user_qen_window_aarch64_qen, VQEAF_USER_QEN_WINDOW_AARCH64_SIZE,
     VQEAF_UID_SECOND_APP, VQEAF_CAPS_APP_UI},
};
#define APP_COUNT ((int)(sizeof(apps)/sizeof(apps[0])))

typedef struct { int pid; int slot; uint64_t base; const char *name; } AmsProc;
static AmsProc procs[VQEAF_AMS_MAX_PROCS];
static int proc_count;
static int next_pid;
static uint64_t ams_root;

void vqeaf_ams_init(uint64_t kernel_root)
{
    ams_root = kernel_root;
    proc_count = 0;
    next_pid = 1;
}

int vqeaf_ams_app_count(void) { return APP_COUNT; }
const char *vqeaf_ams_app_name(int i)
{
    if(i < 0 || i >= APP_COUNT) return 0;
    return apps[i].name;
}

/* A launch is one logical operation: user root, .qen image, task slot, pid
 * and process-table entry. Kernel tasks run at EL1h with IRQ enabled, so a
 * timer tick in the middle used to let a second launch (from EL0 sys_launch)
 * claim the same free task slot: tasks_created drifted to 14 while only 13
 * real tasks existed, and the counter-based shutdown test hung forever.
 * The task_create_user() wrapper is already atomic; shielding the surrounding
 * bookkeeping here keeps pid/procs[] consistent with that slot as well. */
static int ams_launch_locked(const char *name, uint64_t base)
{
    if(!name || !ams_root) return -1;
    const AmsApp *app = 0;
    for(int i = 0; i < APP_COUNT; i++) {
        const char *a = apps[i].name; const char *b = name;
        while(*a && *a == *b) { a++; b++; }
        if(*a == 0 && *b == 0) { app = &apps[i]; break; }
    }
    if(!app) return -2;
    if(proc_count >= (int)VQEAF_AMS_MAX_PROCS) return -3;
    uint64_t root = vqeaf_vm_user_root(ams_root);
    if(!root) return -4;
    uint64_t entry = 0;
    int r = vqeaf_qen_load(root, app->image, app->size, base, &entry);
    if(r != 0) return r;
    VqeafCred cred;
    cred.uid = app->uid;
    cred.gid = VQEAF_GID_APPS;
    cred.caps = app->caps;
    int slot = vqeaf_task_create_user_cred(root, entry,
                   VQEAF_USER_STACK_BASE + VQEAF_USER_STACK_PAGES * VQEAF_PAGE_SIZE,
                   &cred);
    if(slot < 0) return slot;
    int pid = next_pid++;
    procs[proc_count].pid = pid;
    procs[proc_count].slot = slot;
    procs[proc_count].base = base;
    procs[proc_count].name = app->name;
    proc_count++;
    vqeaf_uart_write("AMS: launch ");
    vqeaf_uart_write(app->name);
    vqeaf_uart_write(" -> pid=");
    vqeaf_uart_u64((uint64_t)pid);
    vqeaf_uart_write(" uid=");
    vqeaf_uart_u64((uint64_t)cred.uid);
    vqeaf_uart_write(" caps=");
    vqeaf_uart_hex64(cred.caps);
    vqeaf_uart_write(" base=");
    vqeaf_uart_hex64(base);
    vqeaf_uart_write("\r\n");
    return pid;
}

int vqeaf_ams_launch(const char *name, uint64_t base)
{
    uint64_t daif = vqeaf_a64_irq_save();
    int r = ams_launch_locked(name, base);
    vqeaf_a64_irq_restore(daif);
    return r;
}

int vqeaf_ams_process_slot(int pid)
{
    for(int i = 0; i < proc_count; i++)
        if(procs[i].pid == pid) return procs[i].slot;
    return -1;
}