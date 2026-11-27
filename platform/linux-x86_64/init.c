#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/reboot.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define QEAF_RELEASE "4.14.215"
#define SERVICES_PATH "/qeaf-services"
#define APPS_PATH "/qeapp-runner"
#define SERVICE_READY_TIMEOUT_MS 10000
#define APP_RUN_TIMEOUT_MS 15000
#define SHUTDOWN_TIMEOUT_MS 3000

static volatile sig_atomic_t stop_requested;

static void request_stop(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int make_dir(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return 0;
    dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_FS: FAIL mkdir=%s error=%d\n",
            path, errno);
    return -1;
}

static int mount_fs(const char *target, const char *type, unsigned long flags,
                    const char *options)
{
    if (make_dir(target) != 0)
        return -1;
    if (mount(type, target, type, flags, options) == 0 || errno == EBUSY)
        return 0;
    dprintf(STDERR_FILENO,
            "QEAF_LINUX_X86_64_FS: FAIL mount=%s type=%s error=%d\n",
            target, type, errno);
    return -1;
}

static unsigned long poweroff_delay_ms(void)
{
    char cmdline[512];
    char *token;
    char *end;
    unsigned long value;
    int fd = open("/proc/cmdline", O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t count = read(fd, cmdline, sizeof(cmdline) - 1);
    close(fd);
    if (count <= 0)
        return 0;
    cmdline[count] = '\0';
    token = strstr(cmdline, "qeaf.poweroff_ms=");
    if (token == NULL)
        return 0;
    token += strlen("qeaf.poweroff_ms=");
    errno = 0;
    value = strtoul(token, &end, 10);
    if (errno != 0 || end == token ||
        (*end != '\0' && !isspace((unsigned char)*end)) || value > 60000) {
        dprintf(STDERR_FILENO,
                "QEAF_LINUX_X86_64_INIT: invalid qeaf.poweroff_ms\n");
        return 0;
    }
    return value;
}

static long long monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void sleep_ms(unsigned long milliseconds)
{
    struct timespec delay = {
        (time_t)(milliseconds / 1000),
        (long)((milliseconds % 1000) * 1000000)
    };
    nanosleep(&delay, NULL);
}

static pid_t spawn_process(const char *path, int ready_fd)
{
    pid_t pid = fork();
    char fd_arg[32];

    if (pid != 0)
        return pid;
    if (setpgid(0, 0) != 0) {
        dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL setpgid error=%d\n",
                errno);
        _exit(126);
    }
    if (ready_fd >= 0) {
        int flags = fcntl(ready_fd, F_GETFD);
        if (flags < 0 || fcntl(ready_fd, F_SETFD, flags & ~FD_CLOEXEC) != 0) {
            dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL ready-fd error=%d\n",
                    errno);
            _exit(126);
        }
        snprintf(fd_arg, sizeof(fd_arg), "%d", ready_fd);
        execl(path, path, fd_arg, (char *)NULL);
    } else {
        execl(path, path, (char *)NULL);
    }
    dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL exec=%s error=%d\n",
            path, errno);
    _exit(127);
}

static int wait_service_ready(pid_t pid, int fd)
{
    long long deadline = monotonic_ms() + SERVICE_READY_TIMEOUT_MS;

    if (deadline < SERVICE_READY_TIMEOUT_MS)
        return -1;
    for (;;) {
        struct pollfd descriptor;
        long long remaining = deadline - monotonic_ms();
        int timeout;
        int result;
        char ready = 0;
        ssize_t count;

        if (stop_requested)
            return -1;
        if (remaining <= 0) {
            dprintf(STDERR_FILENO,
                    "QEAF_SERVICES: FAIL ready-timeout pid=%ld timeout_ms=%d\n",
                    (long)pid, SERVICE_READY_TIMEOUT_MS);
            return -1;
        }
        timeout = remaining > 50 ? 50 : (int)remaining;
        memset(&descriptor, 0, sizeof(descriptor));
        descriptor.fd = fd;
        descriptor.events = POLLIN;
        result = poll(&descriptor, 1, timeout);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (result == 0)
            continue;
        if (descriptor.revents & POLLIN) {
            count = read(fd, &ready, 1);
            if (count == 1 && ready == 'R')
                return 0;
            dprintf(STDERR_FILENO,
                    "QEAF_SERVICES: FAIL ready-pipe pid=%ld count=%zd\n",
                    (long)pid, count);
            return -1;
        }
        if (descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            dprintf(STDERR_FILENO, "QEAF_SERVICES: FAIL exited-before-ready pid=%ld\n",
                    (long)pid);
            return -1;
        }
    }
}

static int terminate_process(pid_t pid, int *status)
{
    long long deadline;
    int reaped = 0;

    if (pid <= 0)
        return 0;
    if (kill(-pid, SIGTERM) != 0 && errno == ESRCH)
        (void)kill(pid, SIGTERM);
    deadline = monotonic_ms() + SHUTDOWN_TIMEOUT_MS;
    if (deadline < SHUTDOWN_TIMEOUT_MS)
        return -1;
    while (!reaped) {
        pid_t result = waitpid(pid, status, WNOHANG);
        if (result == pid) {
            reaped = 1;
            break;
        }
        if (result < 0 && errno != EINTR)
            return -1;
        if (monotonic_ms() >= deadline)
            break;
        sleep_ms(20);
    }
    if (!reaped) {
        (void)kill(-pid, SIGKILL);
        if (waitpid(pid, status, 0) != pid)
            return -1;
    }
    (void)kill(-pid, SIGKILL);
    return WIFEXITED(*status) && WEXITSTATUS(*status) == 0 ? 0 : -1;
}

static int run_system(unsigned long poweroff_delay)
{
    int ready_pipe[2] = {-1, -1};
    int service_pid = -1;
    int app_pid = -1;
    int app_done = 0;
    int app_passed = 0;
    int failed = 0;
    int status = 0;
    long long app_deadline;
    long long poweroff_deadline = 0;

    if (pipe2(ready_pipe, O_CLOEXEC) != 0) {
        dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL service-pipe error=%d\n",
                errno);
        return -1;
    }
    service_pid = spawn_process(SERVICES_PATH, ready_pipe[1]);
    close(ready_pipe[1]);
    ready_pipe[1] = -1;
    if (service_pid < 0) {
        dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL service-fork error=%d\n",
                errno);
        failed = 1;
        goto cleanup;
    }
    if (wait_service_ready(service_pid, ready_pipe[0]) != 0) {
        failed = 1;
        goto cleanup;
    }
    close(ready_pipe[0]);
    ready_pipe[0] = -1;

    app_pid = spawn_process(APPS_PATH, -1);
    if (app_pid < 0) {
        dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL app-fork error=%d\n",
                errno);
        failed = 1;
        goto cleanup;
    }
    app_deadline = monotonic_ms() + APP_RUN_TIMEOUT_MS;
    if (app_deadline < APP_RUN_TIMEOUT_MS) {
        failed = 1;
        goto cleanup;
    }

    for (;;) {
        pid_t result;
        long long now = monotonic_ms();

        if (stop_requested)
            break;
        result = waitpid(service_pid, &status, WNOHANG);
        if (result == service_pid) {
            dprintf(STDERR_FILENO,
                    "QEAF_SERVICES: FAIL unexpected-exit pid=%ld status=0x%x\n",
                    (long)service_pid, status);
            service_pid = -1;
            failed = 1;
            break;
        }
        if (result < 0 && errno != EINTR) {
            failed = 1;
            break;
        }
        if (!app_done) {
            result = waitpid(app_pid, &status, WNOHANG);
            if (result == app_pid) {
                app_done = 1;
                app_pid = -1;
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    app_passed = 1;
                    dprintf(STDOUT_FILENO, "QEAF_SYSTEM_APP: PASS runner-exit=0\n");
                    if (poweroff_delay != 0)
                        poweroff_deadline = now + (long long)poweroff_delay;
                } else {
                    dprintf(STDERR_FILENO,
                            "QEAF_SYSTEM_APP: FAIL runner-status=0x%x\n", status);
                    failed = 1;
                    break;
                }
            } else if (result < 0 && errno != EINTR) {
                failed = 1;
                break;
            } else if (now >= app_deadline) {
                dprintf(STDERR_FILENO,
                        "QEAF_SYSTEM_APP: FAIL runner-timeout timeout_ms=%d\n",
                        APP_RUN_TIMEOUT_MS);
                failed = 1;
                break;
            }
        } else if (poweroff_deadline != 0 && now >= poweroff_deadline) {
            break;
        }
        sleep_ms(20);
    }

cleanup:
    if (ready_pipe[0] >= 0)
        close(ready_pipe[0]);
    if (ready_pipe[1] >= 0)
        close(ready_pipe[1]);
    if (app_pid > 0) {
        if (terminate_process(app_pid, &status) != 0 && !stop_requested)
            failed = 1;
        app_pid = -1;
    }
    if (service_pid > 0) {
        if (terminate_process(service_pid, &status) != 0)
            failed = 1;
        service_pid = -1;
    }
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    if (failed) {
        dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_SYSTEM: FAIL\n");
        return -1;
    }
    dprintf(STDOUT_FILENO, "QEAF_LINUX_X86_64_SYSTEM: %s\n",
            app_passed ? "PASS" : "STOPPED");
    return 0;
}

int main(void)
{
    struct utsname info = {0};
    struct sigaction action;
    unsigned long delay_ms;
    int console;
    int result;

    if (getpid() != 1) {
        dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL pid=%ld\n",
                (long)getpid());
        return 1;
    }
    if (uname(&info) != 0 || strcmp(info.release, QEAF_RELEASE) != 0) {
        dprintf(STDERR_FILENO,
                "QEAF_LINUX_X86_64_INIT: FAIL release=%s expected=%s\n",
                info.release, QEAF_RELEASE);
        return 1;
    }

    console = open("/dev/console", O_RDWR);
    if (console >= 0) {
        for (int fd = 0; fd <= 2; ++fd)
            dup2(console, fd);
        if (console > 2)
            close(console);
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGINT, &action, NULL) != 0) {
        dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL sigaction\n");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);

    if (mount_fs("/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0 ||
        mount_fs("/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0 ||
        mount_fs("/dev", "devtmpfs", MS_NOSUID, "mode=0755") != 0 ||
        mount_fs("/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755,size=65536k") != 0 ||
        mount_fs("/tmp", "tmpfs", MS_NOSUID | MS_NODEV,
                 "mode=1777,size=65536k") != 0)
        return 1;

    dprintf(STDOUT_FILENO,
            "QEAF_LINUX_X86_64_FS: READY proc sysfs devtmpfs run tmp\n");
    delay_ms = poweroff_delay_ms();
    dprintf(STDOUT_FILENO,
            "QEAF_LINUX_X86_64_INIT: READY pid=1 release=%s poweroff_ms=%lu\n",
            QEAF_RELEASE, delay_ms);
    result = run_system(delay_ms);
    if (result != 0)
        dprintf(STDERR_FILENO, "QEAF_LINUX_X86_64_INIT: FAIL system\n");
    dprintf(STDOUT_FILENO, "QEAF_LINUX_X86_64_INIT: POWEROFF\n");
    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);
    dprintf(STDERR_FILENO,
            "QEAF_LINUX_X86_64_INIT: FAIL poweroff error=%d\n", errno);
    return 1;
}
