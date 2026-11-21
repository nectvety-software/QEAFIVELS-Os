/* SPDX-License-Identifier: MIT */
/* platform/mt689x/mt689x_session.c - optional Denial Wayland session.
 *
 * Denial (github.com/denialwm/denial) is a compositor, not a kernel: it needs
 * a DRM master node, libinput and a Wayland socket. This file only starts it
 * when the image actually contains it and the session marker asks for it.
 * There is no pretend fallback: if the binary is missing the QEAF shell runs
 * and the status line says so.
 */
#include "mt689x_session.h"
#include "mt689x.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static char note[160] = "session: qeaf shell";

const char *mtk_session_note(void) { return note; }

static int marker_says_denial(void)
{
    char buf[64];
    if (vio_read_file("/etc/qeaf/session", buf, sizeof(buf)) <= 0) return 0;
    for (char *p = buf; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
    return strstr(buf, "denial") != NULL;
}

int mtk_session_boot_choose(unsigned timeout_ms)
{
    enum { MAX_KEYS = 16 };
    struct pollfd pfds[MAX_KEYS];
    DIR *dir = opendir("/dev/input");
    struct dirent *de;
    uint64_t deadline = vio_monotonic_ms() + timeout_ms;
    int nfds = 0, chosen = 0;

    if(!dir) return 0;
    while((de = readdir(dir)) != NULL && nfds < MAX_KEYS) {
        char path[96];
        if(strncmp(de->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if(fd < 0) continue;
        pfds[nfds].fd = fd;
        pfds[nfds].events = POLLIN;
        nfds++;
    }
    closedir(dir);
    if(!nfds) return 0;

    vio_mkdir_p("/etc/qeaf", 0755);
    vio_log("boot menu: VOL+ = Denial, VOL- = QEAF shell (%ums)", timeout_ms);
    while(!chosen && vio_monotonic_ms() < deadline) {
        uint64_t now = vio_monotonic_ms();
        int left = (int)(deadline - now);
        int n = poll(pfds, (nfds_t)nfds, left > 0 ? left : 0);

        if(n <= 0) break;
        for(int i = 0; i < nfds && !chosen; i++) {
            struct input_event ev[8];
            ssize_t got;

            if(!(pfds[i].revents & POLLIN)) continue;
            got = read(pfds[i].fd, ev, sizeof(ev));
            if(got <= 0) continue;
            for(size_t k = 0; k < (size_t)got / sizeof(ev[0]); k++) {
                if(ev[k].type != EV_KEY || ev[k].value != 1) continue;
                if(ev[k].code == KEY_VOLUMEUP) {
                    vio_write_file("/etc/qeaf/session", "denial\n");
                    snprintf(note, sizeof(note), "session: denial (boot menu)");
                    chosen = 1;
                } else if(ev[k].code == KEY_VOLUMEDOWN) {
                    vio_write_file("/etc/qeaf/session", "qeaf\n");
                    snprintf(note, sizeof(note), "session: qeaf shell (boot menu)");
                    chosen = 1;
                }
                if(chosen) break;
            }
        }
    }
    for(int i = 0; i < nfds; i++) close(pfds[i].fd);
    if(chosen) {
        vio_log("boot menu: choice written to /etc/qeaf/session: %s", note);
        return 1;
    }
    return 0;
}

int mtk_session_wants_denial(const char *denial_path)
{
    if (!denial_path || access(denial_path, X_OK) != 0) {
        snprintf(note, sizeof(note), "session: qeaf shell (no denial binary)");
        return 0;
    }
    if (!marker_says_denial()) {
        snprintf(note, sizeof(note), "session: qeaf shell (marker)");
        return 0;
    }
    snprintf(note, sizeof(note), "session: denial (%s)", denial_path);
    return 1;
}

int mtk_session_run_denial(const char *denial_path)
{
    pid_t pid;

    if (!denial_path || access(denial_path, X_OK) != 0) return -1;

    /* A compositor needs a runtime dir it can create its socket in; /run is
     * mounted by vio_setup_environment(). */
    vio_mkdir_p("/run/user/0", 0700);
    setenv("XDG_RUNTIME_DIR", "/run/user/0", 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);

    vio_log("session: starting Denial compositor %s", denial_path);
    pid = fork();
    if (pid < 0) {
        vio_log("session: fork failed: %s", strerror(errno));
        snprintf(note, sizeof(note), "session: denial fork failed");
        return -1;
    }
    if (pid == 0) {
        execl(denial_path, denial_path, NULL);
        _exit(127);
    }

    for (;;) {
        int status = 0;
        pid_t r = waitpid(pid, &status, 0);
        if (r == pid) {
            if (WIFEXITED(status))
                vio_log("session: Denial exited with %d", WEXITSTATUS(status));
            else if (WIFSIGNALED(status))
                vio_log("session: Denial killed by signal %d",
                        WTERMSIG(status));
            return 0;
        }
        if (r < 0 && errno == EINTR) continue;
        return -1;
    }
}
