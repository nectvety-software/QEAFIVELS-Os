/* SPDX-License-Identifier: MIT */
/* platform/violet/host_services.c - native QEAF service manager for Violet.
 *
 * The Linux kernel supplies QRTR transport and Qualcomm drivers. QEAF starts
 * the required userspace daemons itself; no vendor init is part of runtime.
 * pd-mapper and tqftpserv serve real firmware descriptors and firmware blobs.
 * rmtfs -P -r reads actual modemst1/modemst2/fsc/fsg partitions through
 * /dev/disk/by-partlabel using a read-only shadow; it creates no empty EFS.
 * CNSS is the primary Violet service (-n -l); wlfwd is a real-QMI fallback.
 *
 * Missing PD descriptors, NV partitions, board files or factory MAC are
 * reported as unavailable. No synthetic PD maps or generated factory MACs
 * are published, and this code never writes persist or modem NV partitions. */
#define _GNU_SOURCE

#include "host_services.h"

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "violet.h"

#define SERVICE_DIR  "/sbin/qeaf"
#define WLFW_DIR     "/tmp/wlfw"
#define PERSIST_DIR  "/mnt/vendor/persist"

/* Start the WLFW stack after a mainline remoteproc bring-up. */
void vio_host_services_start_wlan(void);
static void launch_qrtr_lookup(void);

struct host_service {
    const char *name;
    pid_t pid;          /* -1 until started */
    int binary_present;
    int exit_status;
    int reaped;
};

static struct host_service services[] = {
    { "pd-mapper", -1, 0, 0, 0 },
    { "pd-listen", -1, 0, 0, 0 }, /* real SERVREG WLAN-PD listener */
    { "tqftpserv", -1, 0, 0, 0 },
    { "rmtfs", -1, 0, 0, 0 },
    { "wlfwd", -1, 0, 0, 0 },   /* QMI protocol fallback */
    { "cnss-daemon", -1, 0, 0, 0 }, /* Violet Qualcomm service */
};
static struct host_service *svc_pd, *svc_pd_listen, *svc_tftp, *svc_rmtfs,
                           *svc_wlfw, *svc_cnss;

static int services_started;
static int wlan_started;
static int wlan_on;
static int wlan_on_errno;
static char wlan_note[192] = "not started";
/* Kept empty until a valid factory address is read from the mounted p74. */
static char wlfw_mac[32];
static pthread_t reaper_thread;
static pthread_t wlan_thread;

static struct host_service *find_svc(const char *name)
{
    for(unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++)
        if(!strcmp(services[i].name, name)) return &services[i];
    return NULL;
}

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int write_text(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    size_t n = strlen(text), done = 0;

    if(fd < 0) return -1;
    while(done < n) {
        ssize_t w = write(fd, text + done, n - done);
        if(w < 0) {
            if(errno == EINTR) continue;
            close(fd);
            return -1;
        }
        done += (size_t)w;
    }
    close(fd);
    return 0;
}

static int read_text(const char *path, char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;

    if(fd < 0) return -1;
    n = read(fd, buf, cap - 1);
    close(fd);
    if(n <= 0) return -1;
    buf[n] = '\0';
    while(n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
    return (int)n;
}

/* Create a character node from a sysfs `dev` file, the same way the modem
 * subsystem node is made. Needed for qcom_rmtfs_mem (rmtfs) and ipa/wlan. */
static int mknod_from_sys(const char *sysdir, const char *name)
{
    char devpath[256], node[320], text[64];
    unsigned maj, min;
    struct stat st;

    if(snprintf(devpath, sizeof(devpath), "%s/%s/dev", sysdir, name) >=
       (int)sizeof(devpath)) return -1;
    if(read_text(devpath, text, sizeof(text)) < 0 ||
       sscanf(text, "%u:%u", &maj, &min) != 2) return -1;
    if(snprintf(node, sizeof(node), "/dev/%s", name) >= (int)sizeof(node))
        return -1;
    if(stat(node, &st) == 0) {
        if(S_ISCHR(st.st_mode) && major(st.st_rdev) == maj &&
           minor(st.st_rdev) == min) return 0;
        unlink(node);
    }
    vio_log("hostsvc: mknod %s (%u:%u)", node, maj, min);
    return mknod(node, S_IFCHR | 0666, makedev(maj, min)) < 0 ? -1 : 0;
}

/* Violet's vendor kernel registers the modem's rmtfs shared memory through
 * msm_sharedmem (DT "qcom,sharedmem-uio", client-id 1): dmesg says
 * "msm_sharedmem: Device created for client 'rmtfs'" and the device shows up
 * as uio0 named "rmtfs" (2 MiB). There is no qcom_rmtfs_mem driver and no
 * rmtfs reserved-memory node, so /dev/qcom_rmtfs_mem1 and the /dev/mem
 * fallback can never exist here. Upstream rmtfs asks for
 * /dev/qcom_rmtfs_uio1 and reads maps/map0/{addr,size}; the UIO driver itself
 * provides the mmap. Publish that node from the real kernel dev number, and
 * only when the UIO device really is the rmtfs one. */
static int publish_rmtfs_uio(void)
{
    static const char *node = "/dev/qcom_rmtfs_uio1";
    DIR *dir = opendir("/sys/class/uio");
    struct dirent *entry;
    int result = -1;

    if(dir == NULL) return -1;
    while((entry = readdir(dir)) != NULL) {
        char base[192], path[256], name[64], text[64], addr[64], size[64];
        unsigned maj, min;
        struct stat st;
        char *end;
        unsigned long long map_addr, map_size;

        if(strncmp(entry->d_name, "uio", 3) != 0 ||
           !isdigit((unsigned char)entry->d_name[3]))
            continue;
        if(snprintf(base, sizeof(base), "/sys/class/uio/%s", entry->d_name) >=
           (int)sizeof(base)) continue;
        snprintf(path, sizeof(path), "%s/name", base);
        if(read_text(path, name, sizeof(name)) < 0 || strcmp(name, "rmtfs"))
            continue;
        snprintf(path, sizeof(path), "%s/dev", base);
        if(read_text(path, text, sizeof(text)) < 0 ||
           sscanf(text, "%u:%u", &maj, &min) != 2)
            continue;
        snprintf(path, sizeof(path), "%s/maps/map0/addr", base);
        if(read_text(path, addr, sizeof(addr)) < 0) continue;
        snprintf(path, sizeof(path), "%s/maps/map0/size", base);
        if(read_text(path, size, sizeof(size)) < 0) continue;
        errno = 0;
        map_addr = strtoull(addr, &end, 16);
        if(errno != 0 || end == addr || *end != '\0') continue;
        errno = 0;
        map_size = strtoull(size, &end, 16);
        if(errno != 0 || end == size || *end != '\0' || map_size == 0 ||
           map_size > SIZE_MAX || map_addr > UINT64_MAX - map_size)
            continue;
        /* Address/size are accepted only from this named kernel UIO map. */

        if(stat(node, &st) == 0) {
            if(S_ISCHR(st.st_mode) && major(st.st_rdev) == maj &&
               minor(st.st_rdev) == min) {
                result = 0;
                break;
            }
            if(unlink(node) != 0) continue;
        }
        if(mknod(node, S_IFCHR | 0666, makedev(maj, min)) != 0) {
            vio_log("hostsvc: cannot publish UIO rmtfs node: %s",
                    strerror(errno));
            continue;
        }
        vio_log("hostsvc: published UIO rmtfs from %s (%u:%u)",
                entry->d_name, maj, min);
        result = 0;
        break;
    }
    closedir(dir);
    return result;
}

/* Use the kernel's real shared-memory device. Violet's vendor kernel exports
 * the region as a UIO device named "rmtfs"; the independent kernel may expose
 * qcom_rmtfs_mem through /sys/class/rmtfs instead. Never fall back to an
 * assumed physical address or an arbitrary UIO node. */
static int publish_rmtfs_sharedmem(void)
{
    if(mknod_from_sys("/sys/class/rmtfs", "qcom_rmtfs_mem1") == 0)
        return 0;
    if(publish_rmtfs_uio() == 0) return 0;
    vio_log("hostsvc: kernel exposes no usable RMTFS memory device");
    return -1;
}

/* Violet's stock kernel has CONFIG_ANDROID_BINDERFS: /proc/filesystems lists
 * "nodev binder" and binderfs's dynamic char region appears in /proc/devices as
 * "499 binder". Nothing is registered in /proc/misc or /sys/class/misc (those
 * only exist for a misc-device binder build), and mknod of a binderfs minor
 * fails with ENXIO because binderfs instances the nodes itself when mounted.
 * Android's ueventd solves this by mounting binderfs; do the same and publish
 * the three real nodes as /dev/binder{,hwbinder,vndbinder} symlinks before
 * cnss-daemon execs. A kernel without binder still fails closed. */
#define BINDERFS_DIR "/dev/binderfs"

static int binderfs_supported(void)
{
    FILE *fp = fopen("/proc/filesystems", "r");
    char line[160];
    int found = 0;

    if(fp == NULL) return 0;
    while(fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;

        while(*p == ' ' || *p == '\t') p++;
        if(strncmp(p, "nodev", 5) == 0) {
            p += 5;
            while(*p == ' ' || *p == '\t') p++;
        }
        if(strncmp(p, "binder", 6) == 0 &&
           (p[6] == '\n' || p[6] == '\0' || p[6] == ' ' || p[6] == '\t'))
            found = 1;
    }
    fclose(fp);
    return found;
}

static int binder_open_works(const char *path)
{
    int fd = open(path, O_RDWR | O_CLOEXEC);

    if(fd < 0) return 0;
    close(fd);
    return 1;
}

static int ensure_binderfs_nodes(void)
{
    static const char *const names[] = { "binder", "hwbinder", "vndbinder" };
    struct stat st;

    if(stat(BINDERFS_DIR, &st) != 0 && vio_mkdir_p(BINDERFS_DIR, 0755) != 0) {
        vio_log("hostsvc: cannot create %s: %s", BINDERFS_DIR, strerror(errno));
        return -1;
    }
    if(mount("binder", BINDERFS_DIR, "binder", 0, NULL) != 0 && errno != EBUSY) {
        vio_log("hostsvc: mount binderfs failed: %s", strerror(errno));
        return -1;
    }

    for(unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char src[160], dst[64];
        struct stat sst, dst_st;

        snprintf(src, sizeof(src), "%s/%s", BINDERFS_DIR, names[i]);
        if(stat(src, &sst) != 0 || !S_ISCHR(sst.st_mode)) continue;
        snprintf(dst, sizeof(dst), "/dev/%s", names[i]);
        if(stat(dst, &dst_st) == 0 && S_ISCHR(dst_st.st_mode) &&
           major(dst_st.st_rdev) == major(sst.st_rdev)) continue;
        unlink(dst);
        if(symlink(src, dst) != 0) {
            vio_log("hostsvc: cannot link %s -> %s: %s", dst, src,
                    strerror(errno));
            continue;
        }
        /* binderfs nodes are 0600 root; cnss-daemon also runs as root, but
         * widen the published node like ueventd does when the fs allows it. */
        (void)chmod(src, 0666);
        if(stat(dst, &dst_st) == 0 && S_ISCHR(dst_st.st_mode))
            vio_log("hostsvc: %s -> %s (%u:%u)", dst, src,
                    major(dst_st.st_rdev), minor(dst_st.st_rdev));
    }
    return binder_open_works("/dev/binder") ? 0 : -1;
}

/* Legacy single-device binder: discover the major at runtime rather than
 * hard-coding it; used only when binderfs is not in the kernel. */
static int char_dev_major(const char *driver, unsigned *major_out)
{
    FILE *fp = fopen("/proc/devices", "r");
    char line[160], name[64] = "";
    unsigned major_no = 0;
    int character_section = 0;

    if(fp == NULL) return -1;
    while(fgets(line, sizeof(line), fp) != NULL) {
        if(strstr(line, "Character devices") != NULL) {
            character_section = 1;
            continue;
        }
        if(strstr(line, "Block devices") != NULL) break;
        if(!character_section) continue;
        if(sscanf(line, "%u %63s", &major_no, name) != 2) continue;
        if(strcmp(name, driver) != 0) continue;
        fclose(fp);
        *major_out = major_no;
        return 0;
    }
    fclose(fp);
    return -1;
}

/* /proc/devices only gives the major. The vendor kernel does not expose a
 * sysfs `dev` file for binder, so probe the handful of minor numbers a char
 * device registered with register_chrdev() can use and keep the one that
 * really opens. This is how we found that 499:0 returns ENXIO on violet. */
static int ensure_binder_node(void)
{
    static const char *node = "/dev/binder";
    unsigned major_no;
    struct stat st;

    if(binder_open_works(node)) return 0;
    if(binderfs_supported() && ensure_binderfs_nodes() == 0) return 0;
    if(char_dev_major("binder", &major_no) != 0) return -1;
    for(unsigned minor_no = 0; minor_no < 8; minor_no++) {
        int fd;

        if(stat(node, &st) != 0 || !S_ISCHR(st.st_mode) ||
           major(st.st_rdev) != major_no || minor(st.st_rdev) != minor_no) {
            unlink(node);
            if(mknod(node, S_IFCHR | 0666,
                     makedev(major_no, minor_no)) < 0) continue;
        }
        fd = open(node, O_RDWR | O_CLOEXEC);
        if(fd < 0) {
            vio_log("hostsvc: binder %u:%u open failed: %s", major_no,
                    minor_no, strerror(errno));
            unlink(node);
            continue;
        }
        close(fd);
        vio_log("hostsvc: binder node ready at %u:%u", major_no, minor_no);
        return 0;
    }
    return -1;
}

static int binder_node_present(void)
{
    struct stat st;

    return stat("/dev/binder", &st) == 0 && S_ISCHR(st.st_mode) &&
           binder_open_works("/dev/binder");
}

/* Publish only real modem NV partitions to upstream rmtfs.  The daemon
 * opens these by PARTNAME and reads the device contents; no empty EFS files
 * or synthetic partition images are created.  rmtfs runs read-only (-r). */
static int prepare_rmtfs_partitions(void)
{
    static const char *const required[] = {
        "modemst1", "modemst2", "fsc", "fsg"
    };
    DIR *blocks = opendir("/sys/class/block");
    struct dirent *de;
    unsigned found = 0;

    if(publish_rmtfs_sharedmem() != 0) return -1;
    if(!blocks) {
        vio_log("hostsvc: cannot enumerate block devices for rmtfs: %s",
                strerror(errno));
        return -1;
    }
    (void)vio_mkdir_p("/dev/disk/by-partlabel", 0755);

    while((de = readdir(blocks)) != NULL) {
        char uevent_path[192], uevent[2048], dev[128], label[64] = "";
        char *p, *e;
        struct stat st;

        if(strncmp(de->d_name, "mmcblk0p", 8) != 0) continue;
        snprintf(uevent_path, sizeof(uevent_path),
                 "/sys/class/block/%s/uevent", de->d_name);
        if(read_text(uevent_path, uevent, sizeof(uevent)) < 0) continue;
        p = strstr(uevent, "PARTNAME=");
        if(!p) continue;
        p += strlen("PARTNAME=");
        e = strchr(p, '\n');
        if(e) {
            size_t n = (size_t)(e - p);
            if(n == 0 || n >= sizeof(label)) continue;
            memcpy(label, p, n);
            label[n] = '\0';
        } else {
            snprintf(label, sizeof(label), "%s", p);
        }

        int wanted = 0;
        for(unsigned i = 0; i < sizeof(required) / sizeof(required[0]); i++)
            if(strcmp(label, required[i]) == 0) wanted = 1;
        if(!wanted) continue;

        snprintf(dev, sizeof(dev), "/dev/%s", de->d_name);
        if(stat(dev, &st) != 0 || !S_ISBLK(st.st_mode)) {
            vio_log("hostsvc: PARTNAME=%s exists but %s is not a block node",
                    label, dev);
            continue;
        }
        char link[192];
        snprintf(link, sizeof(link), "/dev/disk/by-partlabel/%s", label);
        (void)unlink(link);
        if(symlink(dev, link) == 0) {
            found++;
            vio_log("hostsvc: rmtfs partition %s -> %s (read-only)",
                    label, dev);
        } else {
            vio_log("hostsvc: cannot publish %s: %s", link, strerror(errno));
        }
    }
    closedir(blocks);

    if(found != sizeof(required) / sizeof(required[0])) {
        vio_log("hostsvc: refusing rmtfs: found %u/%u required NV partitions",
                found, (unsigned)(sizeof(required) / sizeof(required[0])));
        return -1;
    }
    return 0;
}

/* Mainline ath10k/qcom_pil expect firmware under qcom/qcs615/. The stock
 * layout links everything at /lib/firmware root; mirror it with symlinks. */
static void prepare_mainline_firmware(void)
{
    static const char *dir = "/lib/firmware/qcom/qcs615";
    DIR *d = opendir("/lib/firmware");
    struct dirent *de;
    int made = 0;

    vio_mkdir_p(dir, 0755);
    if(!d) return;
    while((de = readdir(d)) != NULL) {
        char src[256], dst[256];
        const char *n = de->d_name;

        if(!strcmp(n, ".") || !strcmp(n, "..") || !strcmp(n, "qcom") ||
           !strcmp(n, "ath10k")) continue;
        snprintf(src, sizeof(src), "/lib/firmware/%s", n);
        snprintf(dst, sizeof(dst), "%s/%s", dir, n);
        unlink(dst);
        if(symlink(src, dst) == 0) made++;
    }
    closedir(d);
    {
        int dfd = open("/tmp/qeaf-mainline.log",
                       O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        char msg[128];
        snprintf(msg, sizeof(msg), "firmware aliases: %d under %s\n", made, dir);
        if(dfd >= 0) { write(dfd, msg, strlen(msg)); close(dfd); }
    }
    vio_log("hostsvc: %d firmware aliases under %s", made, dir);
}

static void boot_coprocessors(int rmtfs_started)
{
    /* On mainline the DSPs are remoteproc devices; start them now that the
     * firmware symlinks exist. No-op on the vendor kernel. */
    for(unsigned i = 0; i < 4; i++) {
        char base[96], name[64] = "", state[32] = "", path[128];
        snprintf(base, sizeof(base), "/sys/class/remoteproc/remoteproc%u", i);
        if(access(base, F_OK) != 0) continue;
        snprintf(path, sizeof(path), "%s/name", base);
        if(read_text(path, name, sizeof(name)) < 0) continue;
        if(!rmtfs_started &&
           (strstr(name, "mpss") != NULL || strstr(name, "modem") != NULL ||
            strstr(name, "mss") != NULL)) {
            vio_log("hostsvc: remoteproc%u %s held until RMTFS is ready",
                    i, name);
            continue;
        }
        snprintf(path, sizeof(path), "%s/state", base);
        if(read_text(path, state, sizeof(state)) < 0 ||
           strcmp(state, "offline") != 0) continue;
        if(write_text(path, "start") == 0)
            vio_log("hostsvc: remoteproc%u %s started", i, name);
        else
            vio_log("hostsvc: remoteproc%u %s start failed: %s", i, name,
                    strerror(errno));
    }
    /* OnePlus Nord guide: IPA first via /dev/ipa (opening
     * /dev/subsys_ipa_fws directly panics TrustZone on some boards). */
    (void)mknod_from_sys("/sys/class/misc", "ipa");
    /* qcacld registers /dev/wlan under its own class, not misc. */
    (void)mknod_from_sys("/sys/class/wlan", "wlan");
    if(write_text("/dev/ipa", "1") == 0)
        vio_log("hostsvc: IPA started");
    else
        vio_log("hostsvc: /dev/ipa absent: %s", strerror(errno));
    if(write_text("/sys/kernel/boot_adsp/boot", "1") == 0)
        vio_log("hostsvc: ADSP started");
    if(write_text("/sys/kernel/boot_cdsp/boot", "1") == 0)
        vio_log("hostsvc: CDSP started");
}

static void *service_reaper(void *arg)
{
    (void)arg;
    for(;;) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, 0);

        if(pid < 0) {
            if(errno != EINTR) sleep(1);
            continue;
        }
        for(unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++) {
            if(services[i].pid == pid && !services[i].reaped) {
                services[i].exit_status = WIFEXITED(status)
                    ? WEXITSTATUS(status) : -WTERMSIG(status);
                services[i].reaped = 1;
                vio_log("hostsvc: %s exited (status %d)",
                        services[i].name, services[i].exit_status);
            }
        }
    }
    return NULL;
}

static int launch(struct host_service *svc, char *const argv[])
{
    char path[160], logpath[192];
    int null_fd, log_fd, exec_pipe[2], child_errno = 0;
    ssize_t pipe_result;
    pid_t pid;

    if(svc->pid >= 0 && !svc->reaped) return 0; /* already running */
    if(svc->reaped) {
        svc->pid = -1;
        svc->reaped = 0;
    }
    snprintf(path, sizeof(path), "%s/%s", SERVICE_DIR, svc->name);
    if(!path_exists(path)) {
        vio_log("hostsvc: %s missing at %s", svc->name, path);
        return -ENOENT;
    }
    svc->binary_present = 1;
    if(pipe2(exec_pipe, O_CLOEXEC) < 0) {
        vio_log("hostsvc: exec pipe for %s failed: %s",
                svc->name, strerror(errno));
        return -errno;
    }

    pid = fork();
    if(pid < 0) {
        int err = errno;
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        vio_log("hostsvc: fork %s failed: %s", svc->name, strerror(err));
        return -err;
    }
    if(pid == 0) {
        close(exec_pipe[0]);
        setsid();
        null_fd = open("/dev/null", O_RDONLY);
        snprintf(logpath, sizeof(logpath), "/tmp/hostsvc-%s.log", svc->name);
        log_fd = open(logpath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if(null_fd >= 0) dup2(null_fd, STDIN_FILENO);
        if(log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
        }
        if(null_fd > STDERR_FILENO) close(null_fd);
        if(log_fd > STDERR_FILENO) close(log_fd);
        if(strcmp(svc->name, "cnss-daemon") == 0)
            setenv("LD_LIBRARY_PATH", "/system/lib64:/vendor/lib64", 1);
        execv(path, argv);
        child_errno = errno;
        (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }

    close(exec_pipe[1]);
    do {
        pipe_result = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while(pipe_result < 0 && errno == EINTR);
    close(exec_pipe[0]);
    if(pipe_result > 0) {
        svc->pid = -1;
        svc->reaped = 1;
        svc->exit_status = 127;
        vio_log("hostsvc: exec %s failed: %s", svc->name,
                strerror(child_errno));
        return -child_errno;
    }
    if(pipe_result < 0) {
        vio_log("hostsvc: cannot confirm exec for %s: %s",
                svc->name, strerror(errno));
        return -errno;
    }

    svc->pid = pid;
    svc->reaped = 0;
    vio_log("hostsvc: exec confirmed for %s (pid %d)", svc->name, (int)pid);
    return 0;
}

static char *const kPdArgs[]   = { "pd-mapper", NULL };
static char *const kPdListenArgs[] = {
    "pd-listen", "--watch", "msm/modem/wlan_pd", NULL
};
static char *const kTftpArgs[] = { "tqftpserv", "-d", NULL };
static char *const kRmtfsArgs[] = { "rmtfs", "-P", "-r", "-v", NULL };
/* Usage says "-dd for more": pass it as one getopt flag. -n keeps the daemon
 * in the foreground so the launcher's redirected stderr is inherited. */
static char *const kCnssArgs[] = { "cnss-daemon", "-n", "-dd", NULL };

static void rebind_ath10k(void)
{
    static const char *drv = "/sys/bus/platform/drivers/ath10k_snoc";
    char path[160];

    if(access(drv, F_OK) != 0) return;
    snprintf(path, sizeof(path), "%s/unbind", drv);
    if(write_text(path, "18800000.wifi") == 0)
        vio_log("hostsvc: ath10k unbound");
    sleep(1);
    snprintf(path, sizeof(path), "%s/bind", drv);
    if(write_text(path, "18800000.wifi") == 0)
        vio_log("hostsvc: ath10k rebound after modem up");
}

static void *mainline_worker(void *arg)
{
    (void)arg;
    for(unsigned pass = 0; pass < 30; pass++) {
        char diag[160];
        int dfd = open("/tmp/qeaf-mainline.log",
                       O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        snprintf(diag, sizeof(diag), "pass %u: remoteproc=%d\n", pass,
                 path_exists("/sys/class/remoteproc"));
        if(dfd >= 0) { write(dfd, diag, strlen(diag)); close(dfd); }
        if(!path_exists("/sys/class/remoteproc")) { sleep(2); continue; }
        prepare_mainline_firmware();
        /* Only touch remoteproc once the firmware aliases really exist. */
        if(!path_exists("/lib/firmware/qcom/qcs615/adsp.mdt")) {
            sleep(2);
            continue;
        }
        if(svc_rmtfs->reaped) {
            svc_rmtfs->pid = -1;
            svc_rmtfs->reaped = 0;
        }
        if(svc_rmtfs->pid < 0) {
            if(prepare_rmtfs_partitions() == 0)
                (void)launch(svc_rmtfs, kRmtfsArgs);
            else
                vio_log("hostsvc: rmtfs retry skipped; real NV/shared memory unavailable");
        }
        boot_coprocessors(svc_rmtfs->pid > 0 && !svc_rmtfs->reaped);
        for(unsigned i = 0; i < 4; i++) {
            char path[128], state[32] = "";
            snprintf(path, sizeof(path),
                     "/sys/class/remoteproc/remoteproc%u/state", i);
            if(read_text(path, state, sizeof(state)) == 0 &&
               strcmp(state, "online") != 0) goto again;
        }
        vio_log("hostsvc: mainline remoteprocs online");
        rebind_ath10k();
        sleep(2);
        vio_host_services_start_wlan();
        return NULL;
again:
        sleep(2);
    }
    vio_log("hostsvc: mainline remoteprocs never came online");
    return NULL;
}

int vio_host_services_start(void)
{
    int rmtfs_started = 0;

    if(services_started) return 0;
    services_started = 1;
    svc_pd = find_svc("pd-mapper");
    svc_pd_listen = find_svc("pd-listen");
    svc_tftp = find_svc("tqftpserv");
    svc_rmtfs = find_svc("rmtfs");
    svc_wlfw = find_svc("wlfwd");
    svc_cnss = find_svc("cnss-daemon");

    vio_mkdir_p(SERVICE_DIR, 0755);
    prepare_mainline_firmware();
    (void)launch(svc_pd, kPdArgs);
    /* Subscribe to the actual SERVREG service; pd-listen only sends QMI
     * registration after QRTR reports a real 0x42 server endpoint. */
    (void)launch(svc_pd_listen, kPdListenArgs);
    (void)launch(svc_tftp, kTftpArgs);
    if(prepare_rmtfs_partitions() == 0)
        rmtfs_started = launch(svc_rmtfs, kRmtfsArgs) == 0;
    else
        vio_log("hostsvc: rmtfs not started; real NV/shared memory unavailable");
    boot_coprocessors(rmtfs_started);

    if(pthread_create(&reaper_thread, NULL, service_reaper, NULL) == 0)
        pthread_detach(reaper_thread);
    else
        vio_log("hostsvc: no reaper thread: %s", strerror(errno));

    {
        pthread_t ml_thread;
        if(pthread_create(&ml_thread, NULL, mainline_worker, NULL) == 0)
            pthread_detach(ml_thread);
    }
    return 0;
}

/* ---- per-unit WLAN files and the WLFW handshake ------------------------- */

static int scan_tree(const char *dir, const char *name, char *out, size_t cap,
                     int depth)
{
    DIR *d;
    struct dirent *de;
    int n = snprintf(out, cap, "%s/%s", dir, name);

    if(n > 0 && (size_t)n < cap && access(out, F_OK) == 0) return 0;
    if(depth <= 0) return -1;
    d = opendir(dir);
    if(!d) return -1;
    while((de = readdir(d)) != NULL) {
        char child[256];
        struct stat st;

        if(de->d_name[0] == '.') continue;
        if(snprintf(child, sizeof(child), "%s/%s", dir, de->d_name) >=
           (int)sizeof(child)) continue;
        if(stat(child, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if(scan_tree(child, name, out, cap, depth - 1) == 0) {
            closedir(d);
            return 0;
        }
    }
    closedir(d);
    return -1;
}

static int find_persist(const char *name, char *out, size_t cap)
{
    static const char *const roots[] = {
        PERSIST_DIR, "/vendor/firmware", "/lib/firmware", "/vendor/etc/wifi",
        "/vendor/bin", "/vendor/etc/init", "/vendor/lib64",
    };

    for(unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++)
        if(scan_tree(roots[i], name, out, cap, 2) == 0) return 0;
    return -1;
}

static int copy_file(const char *src, const char *dst);
static int read_mac(const char *path, char *out, size_t cap);

int vio_host_find_file(const char *name, char *out, int cap)
{
    return find_persist(name, out, (size_t)cap);
}

static int copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY | O_CLOEXEC);
    int out;
    char buf[8192];
    ssize_t n;

    if(in < 0) return -1;
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if(out < 0) {
        close(in);
        return -1;
    }
    while((n = read(in, buf, sizeof(buf))) > 0) {
        ssize_t done = 0;
        while(done < n) {
            ssize_t w = write(out, buf + done, (size_t)(n - done));
            if(w < 0) {
                if(errno == EINTR) continue;
                close(in); close(out);
                return -1;
            }
            done += w;
        }
    }
    close(in);
    close(out);
    return n < 0 ? -1 : 0;
}

static int mac_hex(unsigned char c)
{
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int mac_is_usable(const unsigned char mac[6])
{
    int all_zero = 1, all_ff = 1;

    for(unsigned i = 0; i < 6; i++) {
        if(mac[i] != 0x00) all_zero = 0;
        if(mac[i] != 0xff) all_ff = 0;
    }
    /* WLAN requires a real unicast address; reject unset and broadcast data. */
    return (mac[0] & 0x01) == 0 && !all_zero && !all_ff;
}

static int parse_mac_text(const unsigned char *text, size_t len,
                          unsigned char mac[6])
{
    size_t first = 0;

    while(first < len && isspace(text[first])) first++;
    while(len > first && isspace(text[len - 1])) len--;
    if(len - first != 17) return -1;
    for(unsigned i = 0; i < 6; i++) {
        size_t at = first + i * 3;
        int hi = mac_hex(text[at]);
        int lo = mac_hex(text[at + 1]);
        if(hi < 0 || lo < 0 || (i < 5 && text[at + 2] != ':'))
            return -1;
        mac[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

static int read_mac(const char *path, char *out, size_t cap)
{
    unsigned char bytes[64], mac[6];
    int fd;
    ssize_t n;

    if(path == NULL || out == NULL || cap < 18) return -1;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) return -1;
    n = read(fd, bytes, sizeof(bytes));
    close(fd);
    if(n == 6)
        memcpy(mac, bytes, sizeof(mac));
    else if(n <= 0 || (size_t)n == sizeof(bytes) ||
            parse_mac_text(bytes, (size_t)n, mac) != 0)
        return -1;
    if(!mac_is_usable(mac)) return -1;
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

/* Do not mistake the empty mountpoint directory in the initramfs for p74. */
static int persist_partition_mounted(void)
{
    FILE *fp = fopen("/proc/mounts", "r");
    char line[512], source[256], mountpoint[256], fstype[64];
    int mounted = 0;

    if(fp == NULL) return 0;
    while(fgets(line, sizeof(line), fp) != NULL) {
        if(sscanf(line, "%255s %255s %63s", source, mountpoint, fstype) != 3)
            continue;
        if(strcmp(mountpoint, PERSIST_DIR) == 0 &&
           strcmp(fstype, "ext4") == 0 &&
           (strstr(source, "persist") != NULL ||
            strstr(source, "mmcblk0p74") != NULL)) {
            mounted = 1;
            break;
        }
    }
    fclose(fp);
    return mounted;
}

static int persist_path_is_local(const char *path)
{
    char resolved[512];
    size_t root_len = strlen(PERSIST_DIR);

    if(path == NULL || realpath(path, resolved) == NULL) return 0;
    return strncmp(resolved, PERSIST_DIR, root_len) == 0 &&
           (resolved[root_len] == '\0' || resolved[root_len] == '/');
}

static int find_factory_mac(char *out, size_t cap, char *path_out,
                            size_t path_cap)
{
    static const char *const paths[] = {
        PERSIST_DIR "/wlan_mac.bin",
        PERSIST_DIR "/wlan/wlan_mac.bin",
        PERSIST_DIR "/wlan/qca_cld/wlan_mac.bin"
    };

    if(!persist_partition_mounted()) return -1;
    for(unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if(!persist_path_is_local(paths[i])) continue;
        if(read_mac(paths[i], out, cap) != 0) continue;
        if(path_out != NULL && path_cap > 0)
            snprintf(path_out, path_cap, "%s", paths[i]);
        return 0;
    }
    return -1;
}

/* Stage only calibration files that already exist on the mounted factory
 * persist partition. The copy is into tmpfs for WLFW; p74 remains read-only. */
static unsigned stage_persist_calibration(void)
{
    unsigned staged = 0;

    if(!persist_partition_mounted()) return 0;
    for(unsigned id = 0; id < 5; id++) {
        char name[32], source[320], target[96];
        struct stat st;

        snprintf(name, sizeof(name), "wlfw_cal_%02u.bin", id);
        if(scan_tree(PERSIST_DIR, name, source, sizeof(source), 4) != 0)
            continue;
        if(!persist_path_is_local(source) || stat(source, &st) != 0 ||
           !S_ISREG(st.st_mode) ||
           st.st_size <= 0 || st.st_size > 16 * 1024 * 1024)
            continue;
        snprintf(target, sizeof(target), "/tmp/wlfw-state/%s", name);
        if(copy_file(source, target) == 0) {
            staged++;
            vio_log("hostsvc: staged factory WLAN calibration id %u (%lld bytes)",
                    id, (long long)st.st_size);
        }
    }
    return staged;
}

/* qcacld's wlan_hdd_state_ctrl_param_write() does
 * copy_from_user(buf, user_buf, 3) unconditionally, so a 2-byte write faults
 * and returns EINVAL. Send exactly three bytes and keep the first form the
 * driver accepts (verified on violet: "ON\n" and 2-byte "ON" both EINVAL). */
static int write_exact(const char *path, const void *buf, size_t len)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    ssize_t w;
    int err;

    if(fd < 0) return -errno;
    do {
        w = write(fd, buf, len);
    } while(w < 0 && errno == EINTR);
    err = errno;
    close(fd);
    if(w < 0) return -err;
    return w == (ssize_t)len ? 0 : -EIO;
}

int vio_host_wlan_on(void)
{
    static const char on_nul[3] = { 'O', 'N', '\0' };
    static const char on_nl[3] = { 'O', 'N', '\n' };
    int rc;

    rc = write_exact("/dev/wlan", on_nul, sizeof(on_nul));
    if(rc != 0) {
        vio_log("hostsvc: wlan ON 'ON\\0' refused: %s", strerror(-rc));
        rc = write_exact("/dev/wlan", on_nl, sizeof(on_nl));
    }
    if(rc != 0) {
        vio_log("hostsvc: wlan ON 'ON\\n' refused: %s", strerror(-rc));
        rc = write_exact("/dev/wlan", "ON", 2);
    }
    wlan_on_errno = rc == 0 ? 0 : -rc;
    if(rc == 0) {
        wlan_on = 1;
        snprintf(wlan_note, sizeof(wlan_note), "wlan ON accepted");
    } else {
        snprintf(wlan_note, sizeof(wlan_note), "wlan ON refused: %s",
                 strerror(wlan_on_errno));
    }
    vio_log("hostsvc: %s", wlan_note);
    return rc;
}

static void *wlan_worker(void *arg)
{
    (void)arg;
    for(unsigned i = 0; i < 12; i++) {
        if(vio_host_wlan_on() == 0) return NULL;
        sleep(2);
    }
    return NULL;
}

void vio_host_services_start_wlan(void)
{
    char path[256], macpath[256];
    int have_board_files = 0;
    int have_factory_mac;
    static char *const wlfw_args[] = { "wlfwd", WLFW_DIR, wlfw_mac,
                                       "/tmp/wlfw-state", NULL };

    if(wlan_started) return;
    wlfw_mac[0] = '\0';
    have_factory_mac = find_factory_mac(wlfw_mac, sizeof(wlfw_mac),
                                        macpath, sizeof(macpath)) == 0;
    if(!have_factory_mac) {
        snprintf(wlan_note, sizeof(wlan_note),
                 "p74 not mounted or factory WLAN MAC absent/invalid; WLAN blocked");
        vio_log("hostsvc: %s", wlan_note);
        launch_qrtr_lookup();
        return;
    }
    /* A failed p74 read is retryable. Do not let either WLAN stack start with
     * the empty/placeholder address while persist is unavailable. */
    wlan_started = 1;

    /* Native QEAF service manager owns CNSS. Match the Violet device-tree
     * invocation (-n -l); the custom WLFW QMI client is fallback-only.
     * CNSS is gated on the real p74 MAC above. It needs /dev/binder even in
     * -n mode; create it from /proc/devices before the first exec. */
    if(ensure_binder_node() != 0)
        vio_log("hostsvc: binder node unavailable; CNSS cannot start");
    if(launch(svc_cnss, kCnssArgs) == 0) {
        snprintf(wlan_note, sizeof(wlan_note),
                 "CNSS daemon spawned by QEAF service manager (-n -l)");
        launch_qrtr_lookup();
        return;
    }
    vio_log("hostsvc: CNSS unavailable; checking native WLFW fallback inputs");
    vio_mkdir_p(WLFW_DIR, 0700);
    vio_mkdir_p("/tmp/wlfw-state", 0700);
    if(stage_persist_calibration() == 0)
        vio_log("hostsvc: p74 has no saved WLFW calibration files; none invented");

    if(find_persist("bdwlan.bin", path, sizeof(path)) == 0 &&
       find_persist("regdb.bin", macpath, sizeof(macpath)) == 0) {
        if(copy_file(path, WLFW_DIR "/bdwlan.bin") == 0 &&
           copy_file(macpath, WLFW_DIR "/regdb.bin") == 0) {
            have_board_files = 1;
            snprintf(wlan_note, sizeof(wlan_note),
                     "real BDF/regdb files copied from mounted firmware or persist");
        } else {
            snprintf(wlan_note, sizeof(wlan_note), "board file copy failed");
        }
    } else {
        snprintf(wlan_note, sizeof(wlan_note),
                 "bdwlan.bin/regdb.bin absent on persist");
    }

    if(have_board_files) {
        vio_log("hostsvc: using factory WLAN MAC from %s", macpath);
        if(launch(svc_wlfw, wlfw_args) == 0) {
            if(pthread_create(&wlan_thread, NULL, wlan_worker, NULL) == 0)
                pthread_detach(wlan_thread);
            else
                vio_log("hostsvc: no wlan thread: %s", strerror(errno));
        }
    } else {
        snprintf(wlan_note, sizeof(wlan_note),
                 "real BDF/regdb unavailable; refusing WLFW fallback");
        vio_log("hostsvc: %s", wlan_note);
    }
    /* No hard-coded pd-listen node/port emulation.  The kernel/QRTR name
     * service and real remote service advertisements are authoritative. */
    launch_qrtr_lookup();
}

int vio_host_wlan_ready(void)
{
    return wlan_on;
}

int vio_host_pd_restart(const char *node, const char *port, const char *name)
{
    char path[160];
    pid_t pid;

    snprintf(path, sizeof(path), "%s/pdr-restart", SERVICE_DIR);
    if(!path_exists(path)) return -1;
    pid = fork();
    if(pid < 0) return -errno;
    if(pid == 0) {
        int fd = open("/tmp/hostsvc-pdr-restart.log",
                      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if(fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); }
        execl(path, "pdr-restart", node, port, name, (char *)NULL);
        _exit(127);
    }
    vio_log("hostsvc: pdr-restart %s %s %s (pid %d)", node, port, name, (int)pid);
    return 0;
}

int vio_host_pd_listen(const char *node, const char *port, const char *name)
{
    char path[160];
    pid_t pid;

    snprintf(path, sizeof(path), "%s/pd-listen", SERVICE_DIR);
    if(!path_exists(path)) return -1;
    pid = fork();
    if(pid < 0) return -errno;
    if(pid == 0) {
        int fd = open("/tmp/hostsvc-pd-listen.log",
                      O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if(fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); }
        execl(path, "pd-listen", node, port, name, (char *)NULL);
        _exit(127);
    }
    vio_log("hostsvc: pd-listen %s %s %s (pid %d)", node, port, name, (int)pid);
    return 0;
}

/* Ask the in-kernel QRTR name service whether WLFW (0x45) is registered. */
static void launch_qrtr_lookup(void)
{
    char path[160];
    pid_t pid;

    snprintf(path, sizeof(path), "%s/qrtr-lookup", SERVICE_DIR);
    if(!path_exists(path)) return;
    pid = fork();
    if(pid < 0) return;
    if(pid == 0) {
        int fd = open("/tmp/hostsvc-qlookup.log",
                      O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if(fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); }
        execl(path, "qrtr-lookup", "0x45", (char *)NULL);
        _exit(127);
    }
}

int vio_host_run_bionic(const char *binary, const char *logname,
                        const char *const *extra_args)
{
    static const char *ldpath =
        "/system/lib64:/vendor/lib64:/vendor/lib:"
        "/apex/com.qeafivels.runtime/lib64/bionic:"
        "/apex/com.qeafivels.runtime/lib64";
    const char *linker = "/system/bin/linker64";
    pid_t pid;

    if(binary == NULL || logname == NULL || access(linker, X_OK) != 0) return -1;
    pid = fork();
    if(pid < 0) return -errno;
    if(pid == 0) {
        char log[160];
        int fd;

        snprintf(log, sizeof(log), "/tmp/hostsvc-%s.log", logname);
        fd = open(log, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if(fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); }
        setenv("LD_LIBRARY_PATH", ldpath, 1);
        /* Bionic linker64 loads the binary directly; library search comes
         * from LD_LIBRARY_PATH (no glibc-style --library-path flag). */
        char *argv[16];
        int argc = 0;
        argv[argc++] = (char *)linker;
        argv[argc++] = (char *)binary;
        for(int i = 0; extra_args != NULL && extra_args[i] != NULL &&
                       argc + 1 < (int)(sizeof(argv)/sizeof(argv[0])); i++)
            argv[argc++] = (char *)extra_args[i];
        argv[argc] = NULL;
        execv(linker, argv);
        _exit(127);
    }
    vio_log("hostsvc: bionic %s -> %s (pid %d)", logname, binary, (int)pid);
    return 0;
}

void vio_host_qrtr_lookup(void) { launch_qrtr_lookup(); }

int vio_host_vendor_wifi(void)
{
    pid_t pid;

    /* cnss-daemon aborts on a missing binder node before it reaches QRTR,
     * so publish the real kernel char device first. This is a /dev node only:
     * no partition, no firmware and no persistent state is written. */
    if(ensure_binder_node() != 0)
        vio_log("hostsvc: binder node unavailable; cnss-daemon will fail fast");

    /* Generate the Bionic linker namespaces config from the runtime APEX so
     * cnss-daemon stops warning about /linkerconfig/ld.config.txt. The APEX
     * is staged under its QEAFIVELS name by package_violet.py. */
    {
        char linkerconfig[128];
        int n = snprintf(linkerconfig, sizeof(linkerconfig),
                         "/apex/com.qeafivels.runtime/bin/linkerconfig");
        if(n > 0 && (size_t)n < sizeof(linkerconfig) &&
           path_exists(linkerconfig)) {
            vio_mkdir_p("/linkerconfig", 0755);
            pid = fork();
            if(pid == 0) {
                int fd = open("/tmp/hostsvc-linkerconfig.log",
                              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                if(fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); }
                execl(linkerconfig, "linkerconfig",
                      "--target", "/linkerconfig", (char *)NULL);
                _exit(127);
            }
            if(pid > 0) waitpid(pid, NULL, 0);
        }
    }
    if(!svc_cnss) svc_cnss = find_svc("cnss-daemon");
    return launch(svc_cnss, kCnssArgs);
}

void vio_host_services_logs(char *out, int cap)
{
    static const char *const names[] = {
        "pd-mapper", "tqftpserv", "rmtfs", "wlfwd", "pdr-restart",
        "pd-listen", "cnss-daemon", "qlookup", "linkerconfig",
        "wpa_supplicant", "gnss-hal"
    };

    if(!out || cap <= 0) return;
    out[0] = '\0';
    for(unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char path[192], text[2048];
        int fd = -1, n;

        snprintf(path, sizeof(path), "/tmp/hostsvc-%s.log", names[i]);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        text[0] = '\0';
        if(fd >= 0) {
            n = read(fd, text, sizeof(text) - 1);
            close(fd);
            if(n > 0) text[n] = '\0';
        }
        snprintf(out + strlen(out), (size_t)(cap - (int)strlen(out)),
                 "--- %s ---\n%s\n", names[i], text[0] ? text : "(no output)");
        if(strlen(out) > (size_t)(cap - 1024)) break;
    }
}

void vio_host_services_report(char *out, int cap)
{
    size_t used;

    if(!out || cap <= 0) return;
    out[0] = '\0';
    used = 0;

    for(unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++) {
        struct host_service *svc = &services[i];
        int n;

        if(used >= (size_t)cap) break;
        if(!svc->binary_present) {
            char probe[160];
            snprintf(probe, sizeof(probe), "%s/%s", SERVICE_DIR, svc->name);
            n = snprintf(out + used, (size_t)cap - used,
                         "hostsvc %s: %s\n", svc->name,
                         path_exists(probe) ? "not started" : "binary missing");
        } else if(svc->reaped)
            n = snprintf(out + used, (size_t)cap - used,
                         "hostsvc %s: exited status=%d\n", svc->name,
                         svc->exit_status);
        else
            n = snprintf(out + used, (size_t)cap - used,
                         "hostsvc %s: running pid=%d\n", svc->name,
                         (int)svc->pid);
        if(n <= 0) break;
        used += (size_t)n;
        if(used > (size_t)cap) used = (size_t)cap;
    }
    if(used < (size_t)cap)
        snprintf(out + used, (size_t)cap - used,
                 "hostsvc binder: %s\n",
                 binder_node_present() ? "present" : "absent");
    if(used < (size_t)cap) used += strlen(out + used);
    if(used < (size_t)cap)
        snprintf(out + used, (size_t)cap - used,
                 "hostsvc wlan: %s (on=%d errno=%d)\n", wlan_note, wlan_on,
                 wlan_on_errno);
}
