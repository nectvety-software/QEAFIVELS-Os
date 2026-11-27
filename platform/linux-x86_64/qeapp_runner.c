#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qeaf_native_abi.h"
#include "vqeaf_qeapp.h"
#include "vqeaf_qeapp_device.h"
#include "vqeaf_qeapp_native.h"
#include "vqeaf_qeapp_sandbox.h"

#define PACKAGE_PATH "/system/apps/puzzle.qeapp"
#define PACKAGE_CAP (512u * 1024u)

static int load_package(uint8_t **out, size_t *size)
{
    struct stat info;
    uint8_t *data;
    size_t used = 0;
    int fd = open(PACKAGE_PATH, O_RDONLY | O_CLOEXEC);

    if(fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) ||
       info.st_size <= 0 || info.st_size > (off_t)PACKAGE_CAP) {
        if(fd >= 0) close(fd);
        return -1;
    }
    data = malloc((size_t)info.st_size);
    if(data == NULL) {
        close(fd);
        return -1;
    }
    while(used < (size_t)info.st_size) {
        ssize_t got = read(fd, data + used, (size_t)info.st_size - used);
        if(got <= 0) {
            free(data);
            close(fd);
            return -1;
        }
        used += (size_t)got;
    }
    close(fd);
    *out = data;
    *size = used;
    return 0;
}

static int valid_lifecycle(const char *lifecycle)
{
    static const char *const required[] = {"create", "start", "stop", "destroy"};
    const char *cursor = lifecycle;

    for(size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        const char *found = strstr(cursor, required[i]);
        if(found == NULL) return 0;
        cursor = found + strlen(required[i]);
    }
    return 1;
}

int main(void)
{
    uint8_t *package = NULL;
    size_t package_size = 0;
    vqeaf_qeapp_native_info_t info;
    const uint8_t *module = NULL;
    size_t module_size = 0;
    char error[512] = {0};
    char report[8192] = {0};
    uint32_t permissions = QEAF_PERM_UI_DISPLAY | QEAF_PERM_INPUT_RECEIVE;
    int result;

    if(geteuid() != 0 || load_package(&package, &package_size) != 0) {
        dprintf(STDERR_FILENO, "QEAPP_RUN: FAIL package-load error=%d\n", errno);
        return 1;
    }
    if(vqeaf_qeapp_inspect_native(package, package_size, &info, &module,
                                  &module_size, error, sizeof(error)) != 0 ||
       info.arch_id != VQEAF_QEN_ARCH_X86_64 || module_size == 0) {
        dprintf(STDERR_FILENO, "QEAPP_RUN: FAIL verify error=%s\n",
                error[0] ? error : "wrong architecture");
        free(package);
        return 1;
    }
    dprintf(STDOUT_FILENO,
            "QEAPP_VERIFY: PASS arch=x86_64 abi=%d module=%zu permissions=0x%x\n",
            info.abi_version, module_size, permissions);

    result = vqeaf_qeapp_device_launch(package, package_size, permissions,
                                       report, sizeof(report), error, sizeof(error));
    if(result == 0)
        dprintf(STDOUT_FILENO, "%s\n", report);
    free(package);

    if(result != 0 || !valid_lifecycle(vqeaf_qeapp_device_lifecycle()) ||
       vqeaf_qeapp_device_hash() == 0 ||
       strstr(vqeaf_qeapp_device_process(), "app uid 20000") == NULL ||
       strstr(vqeaf_qeapp_device_process(), "app gid 20000") == NULL ||
       strstr(vqeaf_qeapp_device_process(), "seccomp") == NULL) {
        dprintf(STDERR_FILENO, "QEAPP_RUN: FAIL error=%s\n",
                error[0] ? error : "missing lifecycle or sandbox evidence");
        return 1;
    }
    dprintf(STDOUT_FILENO,
            "QEAPP_SANDBOX: PASS uid=20000 gid=20000 no_new_privs seccomp\n");
    dprintf(STDOUT_FILENO,
            "QEAPP_RUN: PASS arch=x86_64 abi=%d lifecycle=%s hash=0x%08x\n",
            info.abi_version, vqeaf_qeapp_device_lifecycle(),
            vqeaf_qeapp_device_hash());
    return 0;
}
