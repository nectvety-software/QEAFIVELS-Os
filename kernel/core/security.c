#include "../include/vqeaf/security.h"
#include "../include/vqeaf/el0_abi.h"
_Static_assert(VQEAF_UID_SECOND_APP == VQEAF_UID_FIRST_APP + 1,
               "second app uid must follow the first");
_Static_assert(VQEAF_UID_FIRST_APP + VQEAF_UID_APP_SLOTS <= VQEAF_GID_APPS + 1000u,
               "app uid range must stay in the app band");
/* Capability required by each EL0 syscall number.
 *
 * Syscall numbers come from vqeaf/el0_abi.h, so the policy cannot drift from
 * the ABI. A number absent from this table requires no capability; unknown
 * numbers are still rejected by the dispatcher's default case, so an empty
 * entry never turns into access. */
static const struct { uint64_t nr, cap; } policy[] = {
    { VQEAF_SYS_PUTCHAR,       VQEAF_CAP_CONSOLE },
    { VQEAF_SYS_PRINT,         VQEAF_CAP_CONSOLE },
    { VQEAF_SYS_EXIT,          0 },
    { VQEAF_SYS_SEND,          VQEAF_CAP_IPC_SEND },
    { VQEAF_SYS_RECV,          VQEAF_CAP_IPC_RECV },
    { VQEAF_SYS_SUBMIT_SURFACE, VQEAF_CAP_SURFACE },
    { VQEAF_SYS_POLL_INPUT,    VQEAF_CAP_INPUT },
    { VQEAF_SYS_WINDOW_CREATE, VQEAF_CAP_WINDOW },
    { VQEAF_SYS_WINDOW_PRESENT, VQEAF_CAP_WINDOW },
    { VQEAF_SYS_WINDOW_SET_Z,  VQEAF_CAP_WINDOW },
    { VQEAF_SYS_LAUNCH,        VQEAF_CAP_APP_LAUNCH },
    { VQEAF_SYS_WINDOW_CLOSE,  VQEAF_CAP_WINDOW },
    { VQEAF_SYS_FRAME_DONE,    VQEAF_CAP_SURFACE },
    /* Opening read-only is the baseline; opening for write additionally
     * requires VQEAF_CAP_FS_WRITE, checked by the dispatcher from the flags. */
    { VQEAF_SYS_OPEN,          VQEAF_CAP_FS_READ },
    { VQEAF_SYS_READ,          VQEAF_CAP_FS_READ },
    { VQEAF_SYS_WRITE,         VQEAF_CAP_FS_WRITE },
    { VQEAF_SYS_CLOSE,         0 },
};

uint64_t vqeaf_sec_syscall_cap(uint64_t nr)
{
    for(unsigned i = 0; i < sizeof(policy) / sizeof(policy[0]); i++)
        if(policy[i].nr == nr) return policy[i].cap;
    return 0;
}

int vqeaf_sec_has(const VqeafCred *cred, uint64_t caps)
{
    if(!cred) return 0;
    return (cred->caps & caps) == caps;
}

int vqeaf_sec_allows(const VqeafCred *cred, uint64_t nr)
{
    if(!cred) return 0;
    uint64_t need = vqeaf_sec_syscall_cap(nr);
    return need ? vqeaf_sec_has(cred, need) : 1;
}

int vqeaf_sec_cred_eq(const VqeafCred *a, const VqeafCred *b)
{
    return a && b && a->uid == b->uid && a->gid == b->gid && a->caps == b->caps;
}

int vqeaf_sec_user_cred_ok(const VqeafCred *cred)
{
    if(!cred) return 0;
    if(cred->uid == VQEAF_UID_SYSTEM) return 0;
    if(cred->caps & VQEAF_CAP_PRIVILEGED) return 0;
    return 1;
}
