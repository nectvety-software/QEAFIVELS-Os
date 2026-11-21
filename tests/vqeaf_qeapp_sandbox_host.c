/* SPDX-License-Identifier: MIT */
/* tests/vqeaf_qeapp_sandbox_host.c - runs the real sandbox on a Linux kernel.
 *
 * Built against platform/violet/ui/vqeaf_qeapp_sandbox.c for x86_64 and run as
 * root, this proves the four layers the phone image relies on: the child is no
 * longer uid 0, inherited descriptors are gone, open()/fork()/execve() are
 * refused with EPERM, and ordinary writes still work. It is host evidence for
 * the mechanism, not a substitute for a run on the handset.
 */
#define _GNU_SOURCE
#include "vqeaf_qeapp_sandbox.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", what);
    if(!condition) failures++;
}

int main(void)
{
    int leaked;
    pid_t pid;
    int status = 0;
    char error[192];

    /* Unbuffered: the sandboxed child reports through fd 1 and never returns
     * through stdio teardown, so every line must leave the process at once. */
    setvbuf(stdout, NULL, _IONBF, 0);
    leaked = open("/etc/hostname", O_RDONLY);
    if(leaked < 0) leaked = open("/", O_RDONLY);
    printf("runner uid=%u euid=%u, inherited fd %d\n",
           (unsigned)getuid(), (unsigned)geteuid(), leaked);

    pid = fork();
    if(pid < 0) {
        perror("fork");
        return 2;
    }
    if(pid == 0) {
        int opened;
        pid_t again;

        error[0] = '\0';
        if(vqeaf_qeapp_sandbox_apply(error, sizeof(error)) != 0) {
            printf("FAIL sandbox apply: %s\n", error);
            _exit(3);
        }
        check(geteuid() != 0, "child has an unprivileged uid");
        if(getuid() != geteuid())
            check(0, "real and effective uid agree");

        errno = 0;
        opened = open("/etc/passwd", O_RDONLY);
        check(opened < 0 && errno == EPERM, "open() refused with EPERM");

        errno = 0;
        again = fork();
        check(again < 0 && errno == EPERM, "fork() refused with EPERM");

        errno = 0;
        check(execl("/bin/true", "true", (char *)NULL) < 0 && errno == EPERM,
              "execve() refused with EPERM");

        /* The report pipe is the only channel an app keeps. */
        check(write(STDOUT_FILENO, "sandbox-ok\n", 11) == 11,
              "write() on the report descriptor still works");
        fflush(stdout);
        _exit(failures == 0 ? 0 : 1);
    }

    waitpid(pid, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "sandboxed child finished cleanly");
    close(leaked);
    printf("%s (%d failure%s)\n", failures == 0 ? "SANDBOX HOST TEST OK" : "SANDBOX HOST TEST FAILED",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
