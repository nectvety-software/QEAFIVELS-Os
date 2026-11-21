/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp_sandbox.c - process sandbox for native QEAPP.
 *
 * Four layers, each one checked and none of them advisory:
 *
 *  1. PR_SET_NO_NEW_PRIVS, so no setuid binary can raise the child again and a
 *     seccomp filter is allowed without CAP_SYS_ADMIN.
 *  2. A dedicated unprivileged uid/gid (VQEAF_QEAPP_SANDBOX_UID/GID) replaced
 *     after setgroups(0), so the module cannot touch root-owned files even if
 *     it reached a raw syscall.
 *  3. No inherited descriptor: everything above stderr is closed, RLIMIT_CORE
 *     is 0 and RLIMIT_NOFILE is small, so a leaked display/USB/LUN handle from
 *     the shell cannot be reused.
 *  4. A seccomp allowlist of the syscalls the loader and the module need.
 *     Anything else returns EPERM to the caller instead of killing it, so a
 *     module that reaches outside its API table gets a plain failure rather
 *     than taking the phone down.
 *
 * The file is LVGL-free on purpose: the same code is compiled for the phone
 * (aarch64-linux-musl) and built as a host test on a real Linux kernel.
 */
#include "vqeaf_qeapp_sandbox.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <grp.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

unsigned vqeaf_qeapp_sandbox_uid(void) { return VQEAF_QEAPP_SANDBOX_UID; }
unsigned vqeaf_qeapp_sandbox_gid(void) { return VQEAF_QEAPP_SANDBOX_GID; }

#if defined(__linux__)

#if defined(__aarch64__)
#define SANDBOX_AUDIT_ARCH AUDIT_ARCH_AARCH64
/* asm-generic numbers, plus __ARM_NR_cacheflush: __builtin___clear_cache()
 * after the arena copy may reach the kernel on some compiler revisions. */
static const uint32_t sandbox_allowed[] = {
    0x0f0002u, /* __ARM_NR_cacheflush */
    29u,       /* ioctl: stdio isatty probe on the report pipe */
    57u,       /* close */
    63u,       /* read */
    64u,       /* write: log_utf8 and the run report */
    66u,       /* writev: musl printf/stdio writes the report through writev */
    79u,       /* newfstatat */
    80u,       /* fstat */
    93u,       /* exit */
    94u,       /* exit_group */
    96u,       /* set_tid_address */
    98u,       /* futex */
    99u,       /* set_robust_list */
    101u,      /* nanosleep */
    113u,      /* clock_gettime */
    132u,      /* sigaltstack */
    135u,      /* rt_sigprocmask */
    139u,      /* rt_sigreturn */
    167u,      /* prctl */
    169u,      /* gettimeofday */
    172u,      /* getpid */
    178u,      /* gettid */
    215u,      /* munmap */
    222u,      /* mmap: the .qen arena */
    226u,      /* mprotect */
    233u,      /* madvise */
    278u,      /* getrandom */
    283u,      /* membarrier */
    293u,      /* rseq */
};
#elif defined(__x86_64__)
#define SANDBOX_AUDIT_ARCH AUDIT_ARCH_X86_64
/* Host build only (tests/vqeaf_qeapp_sandbox_host.c): the same layers, verified
 * on a desktop Linux kernel before the phone image is built. */
static const uint32_t sandbox_allowed[] = {
    0u,    /* read */
    1u,    /* write */
    3u,    /* close */
    20u,   /* writev: musl printf/stdio writes the report through writev */
    5u,    /* fstat */
    9u,    /* mmap */
    10u,   /* mprotect */
    11u,   /* munmap */
    12u,   /* brk */
    14u,   /* rt_sigprocmask */
    15u,   /* rt_sigreturn */
    16u,   /* ioctl */
    28u,   /* madvise */
    35u,   /* nanosleep */
    39u,   /* getpid */
    60u,   /* exit */
    96u,   /* gettimeofday */
    131u,  /* sigaltstack */
    157u,  /* prctl */
    158u,  /* arch_prctl */
    186u,  /* gettid */
    202u,  /* futex */
    218u,  /* set_tid_address */
    228u,  /* clock_gettime */
    231u,  /* exit_group */
    262u,  /* newfstatat */
    273u,  /* set_robust_list */
    318u,  /* getrandom */
    324u,  /* membarrier */
    334u,  /* rseq */
};
#else
#error "vqeaf_qeapp_sandbox: no syscall allowlist for this architecture"
#endif

#define SANDBOX_FILTER_MAX (8u + 2u * (sizeof(sandbox_allowed) / sizeof(sandbox_allowed[0])))

#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS SECCOMP_RET_KILL
#endif

static int sandbox_fail(char *error, size_t cap, const char *step, int err)
{
    snprintf(error, cap, "sandbox: %s failed (%d)", step, err ? err : -1);
    return -1;
}

static struct sock_filter bpf_stmt(uint16_t code, uint32_t k)
{
    struct sock_filter insn;
    insn.code = code;
    insn.jt = 0;
    insn.jf = 0;
    insn.k = k;
    return insn;
}

static struct sock_filter bpf_jump(uint16_t code, uint32_t k, uint8_t jt, uint8_t jf)
{
    struct sock_filter insn;
    insn.code = code;
    insn.jt = jt;
    insn.jf = jf;
    insn.k = k;
    return insn;
}

/* Installs the allowlist. Returns 0 only when the kernel accepted it, so a
 * kernel built without CONFIG_SECCOMP_FILTER keeps native apps blocked rather
 * than running them unsandboxed. */
static int sandbox_filter(char *error, size_t cap)
{
    struct sock_filter code[SANDBOX_FILTER_MAX];
    struct sock_fprog program;
    size_t used = 0;
    size_t count = sizeof(sandbox_allowed) / sizeof(sandbox_allowed[0]);

    code[used++] = bpf_stmt(BPF_LD | BPF_W | BPF_ABS,
                            (uint32_t)offsetof(struct seccomp_data, arch));
    code[used++] = bpf_jump(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)SANDBOX_AUDIT_ARCH, 1, 0);
    code[used++] = bpf_stmt(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    code[used++] = bpf_stmt(BPF_LD | BPF_W | BPF_ABS,
                            (uint32_t)offsetof(struct seccomp_data, nr));
    for(size_t i = 0; i < count; i++) {
        code[used++] = bpf_jump(BPF_JMP | BPF_JEQ | BPF_K, sandbox_allowed[i], 0, 1);
        code[used++] = bpf_stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    }
    code[used++] = bpf_stmt(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (uint32_t)(EPERM & SECCOMP_RET_DATA));

    memset(&program, 0, sizeof(program));
    program.len = (uint16_t)used;
    program.filter = code;
    if(prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program, 0, 0) != 0)
        return sandbox_fail(error, cap, "seccomp filter", errno);
    return 0;
}

int vqeaf_qeapp_sandbox_apply(char *error, size_t cap)
{
    struct rlimit limit;

    if(error == NULL || cap == 0) return -1;
    error[0] = '\0';

    /* 1. No privilege can be regained from here on. */
    if(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        return sandbox_fail(error, cap, "PR_SET_NO_NEW_PRIVS", errno);

    /* 2. Drop the shell identity. A runner that is already unprivileged keeps
     * its own uid; either way the child must not be uid 0 any more. */
    if(geteuid() == 0) {
        if(setgroups(0, NULL) != 0)
            return sandbox_fail(error, cap, "setgroups", errno);
        if(setgid((gid_t)VQEAF_QEAPP_SANDBOX_GID) != 0)
            return sandbox_fail(error, cap, "setgid", errno);
        if(setuid((uid_t)VQEAF_QEAPP_SANDBOX_UID) != 0)
            return sandbox_fail(error, cap, "setuid", errno);
    }
    if(geteuid() == 0 || getuid() != geteuid() || getegid() == 0)
        return sandbox_fail(error, cap, "identity separation", EPERM);

    /* 3. No shell descriptor survives the fork: the report pipe was already
     * duplicated onto 0/1/2, so everything from 3 up is a shell handle. */
    {
        long max = sysconf(_SC_OPEN_MAX);
        if(max < 0 || max > 4096) max = 4096;
        for(long fd = 3; fd < max; fd++) close((int)fd);
    }
    limit.rlim_cur = 0;
    limit.rlim_max = 0;
    (void)setrlimit(RLIMIT_CORE, &limit);
    limit.rlim_cur = 64;
    limit.rlim_max = 64;
    (void)setrlimit(RLIMIT_NOFILE, &limit);
    (void)umask(077);
    (void)chdir("/");

    /* 4. Syscall allowlist last, once every setup call above has run. */
    if(sandbox_filter(error, cap) != 0) return -1;
    return 0;
}

#else /* not Linux: the runner only exists on the phone kernel */

int vqeaf_qeapp_sandbox_apply(char *error, size_t cap)
{
    if(error != NULL && cap > 0)
        snprintf(error, cap, "sandbox: no Linux process model on this target");
    return -1;
}

#endif
