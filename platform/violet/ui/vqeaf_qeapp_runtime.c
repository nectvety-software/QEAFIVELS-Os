/* SPDX-License-Identifier: MIT */
/* platform/violet/ui/vqeaf_qeapp_runtime.c - guarded native runtime (Phase 4).
 *
 * The shell never executes a module in its own address space. It spawns the
 * runner (the simulator core compiled as an executable) as a child, waits with
 * a hard timeout, and reads the child's report plus its raw framebuffer. A
 * child crash (signal / NTSTATUS exception) or hang is contained: the supervisor
 * kills/reaps it, reports the failure and returns; the shell keeps running.
 */
#include "vqeaf_qeapp_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

static uint32_t g_canvas_pixels[VQEAF_NATIVE_CANVAS_W * VQEAF_NATIVE_CANVAS_H];

static void parse_report(const char *out, vqeaf_native_report_t *report)
{
    snprintf(report->log, sizeof(report->log), "%s", out);
    const char *line = out;
    while(line && *line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char buf[256];
        if(len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';
        if(strncmp(buf, "lifecycle:", 10) == 0)
            snprintf(report->lifecycle, sizeof(report->lifecycle), "%s", buf + 10);
        else if(strncmp(buf, "outstanding allocs:", 19) == 0)
            report->outstanding_allocs = (size_t)strtoul(buf + 19, NULL, 10);
        else if(strncmp(buf, "framebuffer:", 12) == 0) {
            const char *h = strstr(buf, "hash=0x");
            if(h) report->framebuffer_hash = (uint32_t)strtoul(h + 7, NULL, 16);
        }
        line = end ? end + 1 : NULL;
    }
}

static int load_canvas(const char *fb_path, lv_image_dsc_t *canvas,
                       char *error, size_t cap)
{
    FILE *file = fopen(fb_path, "rb");
    if(file == NULL) {
        snprintf(error, cap, "runtime: child produced no framebuffer");
        return -1;
    }
    size_t count = (size_t)VQEAF_NATIVE_CANVAS_W * VQEAF_NATIVE_CANVAS_H;
    size_t got = fread(g_canvas_pixels, sizeof(uint32_t), count, file);
    fclose(file);
    if(got != count) {
        snprintf(error, cap, "runtime: framebuffer size mismatch");
        return -1;
    }
    memset(canvas, 0, sizeof(*canvas));
    canvas->header.magic = LV_IMAGE_HEADER_MAGIC;
    canvas->header.cf = LV_COLOR_FORMAT_XRGB8888;
    canvas->header.w = VQEAF_NATIVE_CANVAS_W;
    canvas->header.h = VQEAF_NATIVE_CANVAS_H;
    canvas->header.stride = VQEAF_NATIVE_CANVAS_W * 4;
    canvas->data_size = (uint32_t)(count * sizeof(uint32_t));
    canvas->data = (const uint8_t *)g_canvas_pixels;
    return 0;
}

#ifdef _WIN32

static uint64_t now_ms(void)
{
    return (uint64_t)GetTickCount64();
}

static int spawn_runner(const char *runner, const char *pkg, const char *fb,
                        vqeaf_native_report_t *report, char *out, size_t out_cap,
                        char *error, size_t cap)
{
    SECURITY_ATTRIBUTES sa;
    HANDLE rd = NULL, wr = NULL;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[1024];

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if(!CreatePipe(&rd, &wr, &sa, 64 * 1024)) {
        snprintf(error, cap, "runtime: CreatePipe failed");
        return -1;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    memset(&pi, 0, sizeof(pi));
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" --fb \"%s\"", runner, pkg, fb);
    if(!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        snprintf(error, cap, "runtime: cannot spawn runner");
        return -1;
    }
    CloseHandle(wr);

    DWORD wait = WaitForSingleObject(pi.hProcess, VQEAF_NATIVE_TIMEOUT_MS);
    if(wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        report->timed_out = 1;
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    report->exit_code = (int)code;
    report->crashed = (code >= 0xC0000000u);

    size_t used = 0;
    for(;;) {
        DWORD got = 0;
        if(used + 1 >= out_cap) break;
        if(!ReadFile(rd, out + used, (DWORD)(out_cap - 1 - used), &got, NULL) || got == 0)
            break;
        used += got;
    }
    out[used] = '\0';
    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    (void)now_ms;
    return 0;
}

static int make_fb_path(char *buf, size_t cap)
{
    if(GetTempFileNameA(".", "vqn", 0, buf) == 0) return -1;
    (void)cap;
    return 0;
}

#else /* POSIX */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static int spawn_runner(const char *runner, const char *pkg, const char *fb,
                        vqeaf_native_report_t *report, char *out, size_t out_cap,
                        char *error, size_t cap)
{
    int pipefd[2];
    if(pipe(pipefd) != 0) {
        snprintf(error, cap, "runtime: pipe failed");
        return -1;
    }
    pid_t pid = fork();
    if(pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(error, cap, "runtime: fork failed");
        return -1;
    }
    if(pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execl(runner, runner, pkg, "--fb", fb, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);

    int status = 0;
    uint64_t deadline = now_ms() + VQEAF_NATIVE_TIMEOUT_MS;
    for(;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if(r == pid) break;
        if(r < 0) break;
        if(now_ms() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            report->timed_out = 1;
            break;
        }
        usleep(10000);
    }
    report->crashed = WIFSIGNALED(status);
    report->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    size_t used = 0;
    for(;;) {
        if(used + 1 >= out_cap) break;
        ssize_t got = read(pipefd[0], out + used, out_cap - 1 - used);
        if(got <= 0) break;
        used += (size_t)got;
    }
    out[used] = '\0';
    close(pipefd[0]);
    return 0;
}

static int make_fb_path(char *buf, size_t cap)
{
    snprintf(buf, cap, "/tmp/vqeaf-native-XXXXXX");
    int fd = mkstemp(buf);
    if(fd < 0) return -1;
    close(fd);
    return 0;
}

#endif

int vqeaf_qeapp_native_launch(const char *runner_path, const char *package_path,
                              lv_image_dsc_t *canvas,
                              vqeaf_native_report_t *report,
                              char *error, size_t cap)
{
    char fb[512];
    char out[4096];
    int rc = -1;

    if(runner_path == NULL || package_path == NULL || report == NULL) {
        snprintf(error, cap, "runtime: missing arguments");
        return -1;
    }
    memset(report, 0, sizeof(*report));
    out[0] = '\0';
    if(make_fb_path(fb, sizeof(fb)) != 0) {
        snprintf(error, cap, "runtime: cannot create temp framebuffer");
        return -1;
    }

    if(spawn_runner(runner_path, package_path, fb, report, out, sizeof(out),
                    error, cap) != 0) {
        remove(fb);
        return -1;
    }
    parse_report(out, report);

    if(report->timed_out) {
        snprintf(error, cap, "runtime: module exceeded %d ms and was killed",
                 VQEAF_NATIVE_TIMEOUT_MS);
        rc = -1;
    } else if(report->crashed) {
        snprintf(error, cap, "runtime: module crashed (exit 0x%x); shell survived",
                 (unsigned)report->exit_code);
        rc = -1;
    } else if(report->exit_code != 0) {
        snprintf(error, cap, "runtime: module run failed (exit %d)",
                 report->exit_code);
        rc = -1;
    } else if(canvas != NULL) {
        rc = load_canvas(fb, canvas, error, cap);
    } else {
        rc = 0;
    }
    remove(fb);
    return rc;
}