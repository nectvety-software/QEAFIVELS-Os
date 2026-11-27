#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vqeaf_qeapp.h"
#include "vqeaf_qeapp_native.h"
#include "vqeaf/service.h"

#define PACKAGE_PATH "/system/apps/puzzle.qeapp"
#define PACKAGE_CAP (512u * 1024u)
#define SYSTEM_CALLER 0u

typedef struct {
    int package_fd;
    char app_id[64];
    int abi_version;
    int arch_id;
} qeaf_service_context_t;

static volatile sig_atomic_t stop_requested;
static qeaf_service_context_t g_context;

static void request_stop(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int package_store_start(void *ctx)
{
    qeaf_service_context_t *context = ctx;
    struct stat info = {0};

    context->package_fd = open(PACKAGE_PATH, O_RDONLY | O_CLOEXEC);
    if(context->package_fd < 0) {
        dprintf(STDERR_FILENO, "QEAF_SERVICE: FAIL package-store open error=%d\n",
                errno);
        return -1;
    }
    if(fstat(context->package_fd, &info) != 0 || !S_ISREG(info.st_mode) ||
       info.st_size <= 0 || info.st_size > (off_t)PACKAGE_CAP) {
        dprintf(STDERR_FILENO,
                "QEAF_SERVICE: FAIL package-store size=%lld error=%d\n",
                (long long)info.st_size, errno);
        close(context->package_fd);
        context->package_fd = -1;
        return -1;
    }
    dprintf(STDOUT_FILENO, "QEAF_SERVICE: START name=package-store pid=%ld\n",
            (long)getpid());
    return 0;
}

static int package_store_stop(void *ctx)
{
    qeaf_service_context_t *context = ctx;

    if(context->package_fd >= 0) close(context->package_fd);
    context->package_fd = -1;
    dprintf(STDOUT_FILENO, "QEAF_SERVICE: STOP name=package-store pid=%ld\n",
            (long)getpid());
    return 0;
}

static int qeapp_preflight_start(void *ctx)
{
    qeaf_service_context_t *context = ctx;
    vqeaf_qeapp_native_info_t info;
    const uint8_t *module = NULL;
    size_t module_size = 0;
    size_t size = (size_t)lseek(context->package_fd, 0, SEEK_END);
    uint8_t *package;
    size_t used = 0;
    char error[256] = {0};
    int result;

    if(size == 0 || size > PACKAGE_CAP || lseek(context->package_fd, 0, SEEK_SET) < 0)
        return -1;
    package = malloc(size);
    if(package == NULL) return -1;
    while(used < size) {
        ssize_t got = pread(context->package_fd, package + used, size - used,
                            (off_t)used);
        if(got <= 0) {
            free(package);
            return -1;
        }
        used += (size_t)got;
    }
    result = vqeaf_qeapp_inspect_native(package, size, &info, &module, &module_size,
                                        error, sizeof(error));
    free(package);
    if(result != 0 || module == NULL || module_size == 0 ||
       info.arch_id != VQEAF_QEN_ARCH_X86_64) {
        dprintf(STDERR_FILENO,
                "QEAF_SERVICE: FAIL qeapp-preflight error=%s\n",
                error[0] ? error : "wrong architecture or empty module");
        return -1;
    }
    context->abi_version = info.abi_version;
    context->arch_id = info.arch_id;
    (void)module;
    dprintf(STDOUT_FILENO,
            "QEAF_SERVICE: START name=qeapp-preflight pid=%ld abi=%d arch=x86_64\n",
            (long)getpid(), info.abi_version);
    return 0;
}

static int qeapp_preflight_stop(void *ctx)
{
    (void)ctx;
    dprintf(STDOUT_FILENO, "QEAF_SERVICE: STOP name=qeapp-preflight pid=%ld\n",
            (long)getpid());
    return 0;
}

int main(int argc, char **argv)
{
    static const VqeafServiceDef definitions[] = {
        {
            "package-store", 1u, package_store_start, package_store_stop,
            &g_context, {NULL, NULL, NULL, NULL},
            {"package.read", NULL, NULL, NULL}
        },
        {
            "qeapp-preflight", 1u, qeapp_preflight_start, qeapp_preflight_stop,
            &g_context, {"package-store", NULL, NULL, NULL},
            {"app.launch", NULL, NULL, NULL}
        }
    };
    struct sigaction action;
    sigset_t blocked_signals;
    sigset_t previous_mask;
    int ready_fd;
    int runtime_id;
    char ready = 'R';

    if(argc != 2 || getpid() == 1 || geteuid() != 0) return 1;
    g_context.package_fd = -1;
    ready_fd = atoi(argv[1]);
    if(ready_fd < 3) return 1;

    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    if(sigaction(SIGTERM, &action, NULL) != 0 ||
       sigaction(SIGINT, &action, NULL) != 0)
        return 1;
    signal(SIGPIPE, SIG_IGN);
    sigemptyset(&blocked_signals);
    sigaddset(&blocked_signals, SIGTERM);
    sigaddset(&blocked_signals, SIGINT);
    if(sigprocmask(SIG_BLOCK, &blocked_signals, &previous_mask) != 0)
        return 1;

    vqeaf_svc_reset();
    if(vqeaf_svc_register(&definitions[0]) < 0 ||
       vqeaf_svc_register(&definitions[1]) < 0 ||
       vqeaf_svc_grant(SYSTEM_CALLER, "package.read") != 0 ||
       vqeaf_svc_grant(SYSTEM_CALLER, "app.launch") != 0)
        return 1;
    runtime_id = vqeaf_svc_find("qeapp-preflight");
    if(runtime_id < 0 || vqeaf_svc_start((uint32_t)runtime_id, SYSTEM_CALLER) != 0 ||
       vqeaf_svc_state((uint32_t)runtime_id) != VQEAF_SVC_RUNNING)
        return 1;

    dprintf(STDOUT_FILENO,
            "QEAF_SERVICES: READY pid=%ld services=2 abi=%d arch=x86_64\n",
            (long)getpid(), g_context.abi_version);
    if(write(ready_fd, &ready, 1) != 1) {
        vqeaf_svc_stop_all(SYSTEM_CALLER);
        return 1;
    }
    close(ready_fd);

    while(!stop_requested) sigsuspend(&previous_mask);
    if(vqeaf_svc_stop_all(SYSTEM_CALLER) != 0 ||
       vqeaf_svc_state(0) != VQEAF_SVC_STOPPED ||
       vqeaf_svc_state(1) != VQEAF_SVC_STOPPED)
        return 1;
    dprintf(STDOUT_FILENO, "QEAF_SERVICES: STOPPED pid=%ld\n", (long)getpid());
    return 0;
}
