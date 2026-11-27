/* SPDX-License-Identifier: MIT */
/* Native Wi-Fi control for QEAF Violet.
 *
 * The kernel owns WCN3990/ICNSS. wpa_supplicant owns 802.11 authentication
 * through its Unix control socket. This file never shells out and never logs
 * credentials. Association is asynchronous: a successful CONNECT command
 * means the supplicant accepted the profile, not that the AP is connected.
 */
#define _GNU_SOURCE
#include "vqeaf_wifi.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "vqeaf_state.h"

static const char *wifi_ifname(void);

#ifdef __linux__
#include <arpa/inet.h>
#include <ctype.h>
#include <ifaddrs.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define WPA_CTRL_PATH "/run/wpa_supplicant/wlan0"
#define WPA_REPLY_MAX 8192

static unsigned ctrl_serial;

static int wifi_ctrl_open(void)
{
    struct sockaddr_un local, remote;
    char local_path[sizeof(local.sun_path)];
    int fd, n;

    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if(fd < 0) return -errno;

    memset(&local, 0, sizeof(local));
    local.sun_family = AF_UNIX;
    n = snprintf(local_path, sizeof(local_path), "/tmp/qeaf-wpa-%ld-%u",
                 (long)getpid(), ++ctrl_serial);
    if(n < 0 || (size_t)n >= sizeof(local.sun_path)) {
        close(fd);
        return -ENAMETOOLONG;
    }
    snprintf(local.sun_path, sizeof(local.sun_path), "%s", local_path);
    unlink(local.sun_path);
    if(bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
        int err = errno;
        close(fd);
        return -err;
    }

    memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    snprintf(remote.sun_path, sizeof(remote.sun_path), "%s", WPA_CTRL_PATH);
    if(connect(fd, (struct sockaddr *)&remote, sizeof(remote)) != 0) {
        int err = errno;
        unlink(local.sun_path);
        close(fd);
        return -err;
    }
    return fd;
}

static void wifi_ctrl_close(int fd)
{
    struct sockaddr_un local;
    socklen_t len = sizeof(local);
    if(fd < 0) return;
    memset(&local, 0, sizeof(local));
    if(getsockname(fd, (struct sockaddr *)&local, &len) == 0 &&
       local.sun_family == AF_UNIX && local.sun_path[0] != '\0')
        unlink(local.sun_path);
    close(fd);
}

static int wifi_ctrl_request(int fd, const char *command,
                             char *reply, size_t capacity)
{
    struct pollfd pfd;
    ssize_t n;
    size_t length;

    if(fd < 0 || command == NULL || reply == NULL || capacity < 2)
        return -EINVAL;
    length = strlen(command);
    if(length == 0 || length > 1024) return -EINVAL;
    do { n = send(fd, command, length, 0); } while(n < 0 && errno == EINTR);
    if(n < 0) return -errno;
    if((size_t)n != length) return -EIO;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    do { n = poll(&pfd, 1, 3000); } while(n < 0 && errno == EINTR);
    if(n == 0) return -ETIMEDOUT;
    if(n < 0) return -errno;
    if(!(pfd.revents & POLLIN)) return -EIO;
    do { n = recv(fd, reply, capacity - 1, 0); }
    while(n < 0 && errno == EINTR);
    if(n < 0) return -errno;
    reply[n] = '\0';
    while(n > 0 && (reply[n - 1] == '\n' || reply[n - 1] == '\r'))
        reply[--n] = '\0';
    return (int)n;
}

static int wifi_ctrl_ok(int fd, const char *command)
{
    char reply[256];
    int rc = wifi_ctrl_request(fd, command, reply, sizeof(reply));
    if(rc < 0) return rc;
    return strcmp(reply, "OK") == 0 ? 0 : -EIO;
}

static void add_network(vqeaf_wifi_net_t *out, int max, int *count,
                        const char *ssid, const char *bssid,
                        int signal, int enc)
{
    int secure = enc != VQEAF_WIFI_ENC_OPEN;
    if(ssid == NULL || ssid[0] == '\0') return;
    for(int i = 0; i < *count; i++) {
        if(strcmp(out[i].ssid, ssid) == 0) {
            if(signal > out[i].signal) {
                out[i].signal = signal;
                snprintf(out[i].bssid, sizeof(out[i].bssid), "%s", bssid);
            }
            if(enc > out[i].enc) out[i].enc = enc;
            out[i].secure |= secure;
            return;
        }
    }
    if(*count >= max) return;
    snprintf(out[*count].ssid, sizeof(out[*count].ssid), "%s", ssid);
    snprintf(out[*count].bssid, sizeof(out[*count].bssid), "%s", bssid);
    out[*count].signal = signal;
    out[*count].secure = secure;
    out[*count].enc = enc;
    (*count)++;
}

static int parse_scan_results(char *reply, vqeaf_wifi_net_t *out, int max)
{
    char *save = NULL;
    char *line = strtok_r(reply, "\n", &save);
    int count = 0;
    if(line == NULL) return -EPROTO; /* header */
    while((line = strtok_r(NULL, "\n", &save)) != NULL) {
        char *field[5], *p = line, *tab;
        int signal, enc = VQEAF_WIFI_ENC_OPEN;
        for(int i = 0; i < 4; i++) {
            field[i] = p;
            tab = strchr(p, '\t');
            if(tab == NULL) { p = NULL; break; }
            *tab = '\0';
            p = tab + 1;
        }
        if(p == NULL) continue;
        field[4] = p;
        signal = atoi(field[2]);
        /* wpa_supplicant reports RSSI in dBm (usually -100..-30). */
        signal = (signal + 100) * 100 / 70;
        if(signal < 0) signal = 0;
        if(signal > 100) signal = 100;
        if(strstr(field[3], "WPA-PSK") != NULL)
            enc = VQEAF_WIFI_ENC_WPA;
        if(strstr(field[3], "WPA2-PSK") != NULL ||
           strstr(field[3], "RSN-PSK") != NULL)
            enc = VQEAF_WIFI_ENC_WPA2_PSK;
        if(strstr(field[3], "WEP") != NULL)
            enc = VQEAF_WIFI_ENC_WEP;
        if(strstr(field[3], "SAE") != NULL &&
           enc == VQEAF_WIFI_ENC_OPEN)
            enc = VQEAF_WIFI_ENC_WPA3_SAE;
        if(strstr(field[3], "OWE") != NULL)
            enc = VQEAF_WIFI_ENC_OWE;
        if(strstr(field[3], "WPA-EAP") != NULL ||
           strstr(field[3], "WPA2-EAP") != NULL ||
           strstr(field[3], "IEEE8021X") != NULL)
            enc = VQEAF_WIFI_ENC_EAP;
        add_network(out, max, &count, field[4], field[0], signal, enc);
    }
    for(int i = 1; i < count; i++) {
        vqeaf_wifi_net_t key = out[i];
        int j = i - 1;
        while(j >= 0 && out[j].signal < key.signal) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return count;
}

int vqeaf_wifi_scan(vqeaf_wifi_net_t *out, int max)
{
    char reply[WPA_REPLY_MAX];
    int fd, rc;
    if(out == NULL || max <= 0) return -EINVAL;
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    fd = wifi_ctrl_open();
    if(fd < 0) return fd;
    rc = wifi_ctrl_ok(fd, "SCAN");
    if(rc == 0) {
        /* Scan is asynchronous; give the driver a short window to populate
         * SCAN_RESULTS. The UI already performs scans synchronously. */
        usleep(1500000);
        rc = wifi_ctrl_request(fd, "SCAN_RESULTS", reply, sizeof(reply));
        if(rc >= 0) rc = parse_scan_results(reply, out, max);
    }
    wifi_ctrl_close(fd);
    return rc;
}

static int set_network_value(int fd, int id, const char *key,
                             const char *value)
{
    char command[1200];
    int n = snprintf(command, sizeof(command), "SET_NETWORK %d %s %s",
                     id, key, value);
    int rc;
    if(n < 0 || (size_t)n >= sizeof(command)) return -E2BIG;
    rc = wifi_ctrl_ok(fd, command);
    memset(command, 0, sizeof(command));
    return rc;
}

static int quote_secret(char *out, size_t cap, const char *secret,
                        size_t length)
{
    size_t used = 0;
    if(cap < 3) return -E2BIG;
    out[used++] = '"';
    for(size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)secret[i];
        if(c < 0x20 || c > 0x7e || used + 3 >= cap) return -EINVAL;
        if(c == '"' || c == '\\') out[used++] = '\\';
        out[used++] = (char)c;
    }
    out[used++] = '"';
    out[used] = '\0';
    return 0;
}

int vqeaf_wifi_connect(const char *ssid, const char *password, int enc)
{
    char reply[256], command[1200], ssid_hex[65], secret[260];
    size_t ssid_len, pass_len = password ? strlen(password) : 0;
    const char *key_mgmt = NULL, *secret_key = NULL;
    int fd, rc, id;

    if(ssid == NULL || ssid[0] == '\0') return -EINVAL;
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    ssid_len = strlen(ssid);
    if(ssid_len > 32) return -EINVAL;

    switch(enc) {
    case VQEAF_WIFI_ENC_OPEN:
        if(pass_len != 0) return -EINVAL;
        key_mgmt = "NONE";
        break;
    case VQEAF_WIFI_ENC_WPA:
    case VQEAF_WIFI_ENC_WPA2_PSK:
    case VQEAF_WIFI_ENC_WPA_WPA2:
        if(pass_len < 8 || pass_len > 64) return -EINVAL;
        if(pass_len == 64) {
            for(size_t i = 0; i < pass_len; i++)
                if(!isxdigit((unsigned char)password[i])) return -EINVAL;
            snprintf(secret, sizeof(secret), "%s", password);
        } else {
            rc = quote_secret(secret, sizeof(secret), password, pass_len);
            if(rc != 0) return rc;
        }
        key_mgmt = "WPA-PSK";
        secret_key = "psk";
        break;
    case VQEAF_WIFI_ENC_WPA3_SAE:
        if(pass_len == 0 || pass_len > 63) return -EINVAL;
        rc = quote_secret(secret, sizeof(secret), password, pass_len);
        if(rc != 0) return rc;
        key_mgmt = "SAE";
        secret_key = "sae_password";
        break;
    case VQEAF_WIFI_ENC_OWE:
        if(pass_len != 0) return -EINVAL;
        key_mgmt = "OWE";
        break;
    case VQEAF_WIFI_ENC_WEP:
    case VQEAF_WIFI_ENC_EAP:
    default:
        return -EOPNOTSUPP;
    }

    fd = wifi_ctrl_open();
    if(fd < 0) { memset(secret, 0, sizeof(secret)); return fd; }
    for(size_t i = 0; i < ssid_len; i++)
        snprintf(ssid_hex + i * 2, sizeof(ssid_hex) - i * 2, "%02x",
                 (unsigned char)ssid[i]);
    ssid_hex[ssid_len * 2] = '\0';

    rc = wifi_ctrl_request(fd, "ADD_NETWORK", reply, sizeof(reply));
    if(rc < 0 || sscanf(reply, "%d", &id) != 1) {
        wifi_ctrl_close(fd);
        memset(secret, 0, sizeof(secret));
        return rc < 0 ? rc : -EPROTO;
    }
    snprintf(command, sizeof(command), "SET_NETWORK %d ssid %s", id, ssid_hex);
    rc = wifi_ctrl_ok(fd, command);
    if(rc == 0) rc = set_network_value(fd, id, "key_mgmt", key_mgmt);
    if(rc == 0 && secret_key != NULL)
        rc = set_network_value(fd, id, secret_key, secret);
    if(rc == 0 && (enc == VQEAF_WIFI_ENC_WPA3_SAE ||
                   enc == VQEAF_WIFI_ENC_OWE))
        rc = set_network_value(fd, id, "ieee80211w", "2");
    memset(secret, 0, sizeof(secret));

    if(rc == 0) {
        snprintf(command, sizeof(command), "ENABLE_NETWORK %d", id);
        rc = wifi_ctrl_ok(fd, command);
    }
    if(rc == 0) {
        snprintf(command, sizeof(command), "SELECT_NETWORK %d", id);
        rc = wifi_ctrl_ok(fd, command);
    }
    if(rc != 0) {
        snprintf(command, sizeof(command), "REMOVE_NETWORK %d", id);
        (void)wifi_ctrl_ok(fd, command);
    }
    wifi_ctrl_close(fd);
    return rc;
}

int vqeaf_wifi_disconnect(void)
{
    int fd, rc;
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    fd = wifi_ctrl_open();
    if(fd < 0) return fd;
    rc = wifi_ctrl_ok(fd, "DISCONNECT");
    wifi_ctrl_close(fd);
    return rc;
}

static void copy_status_field(char *dst, size_t cap,
                              const char *line, const char *key)
{
    size_t n = strlen(key);
    if(strncmp(line, key, n) == 0 && line[n] == '=') {
        snprintf(dst, cap, "%s", line + n + 1);
    }
}

int vqeaf_wifi_get_status(vqeaf_wifi_status_t *status)
{
    char reply[WPA_REPLY_MAX], *save = NULL, *line;
    struct ifaddrs *ifas = NULL;
    int fd, rc;
    if(status == NULL) return -EINVAL;
    memset(status, 0, sizeof(*status));
    snprintf(status->state, sizeof(status->state), "UNAVAILABLE");
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    fd = wifi_ctrl_open();
    if(fd < 0) return fd;
    rc = wifi_ctrl_request(fd, "STATUS", reply, sizeof(reply));
    wifi_ctrl_close(fd);
    if(rc < 0) return rc;
    line = strtok_r(reply, "\n", &save);
    while(line != NULL) {
        copy_status_field(status->state, sizeof(status->state), line, "wpa_state");
        copy_status_field(status->ssid, sizeof(status->ssid), line, "ssid");
        copy_status_field(status->bssid, sizeof(status->bssid), line, "bssid");
        copy_status_field(status->ip_address, sizeof(status->ip_address), line, "ip_address");
        line = strtok_r(NULL, "\n", &save);
    }
    status->connected = strcmp(status->state, "COMPLETED") == 0;
    if(status->ip_address[0] == '\0' && getifaddrs(&ifas) == 0) {
        for(struct ifaddrs *it = ifas; it != NULL; it = it->ifa_next) {
            if(it->ifa_addr == NULL || strcmp(it->ifa_name, wifi_ifname()) != 0 ||
               it->ifa_addr->sa_family != AF_INET) continue;
            if(inet_ntop(AF_INET,
                         &((struct sockaddr_in *)it->ifa_addr)->sin_addr,
                         status->ip_address, sizeof(status->ip_address)) != NULL)
                break;
        }
        freeifaddrs(ifas);
    }
    return 0;
}

#else /* host render harness: deterministic, explicitly simulated data */
int vqeaf_wifi_scan(vqeaf_wifi_net_t *out, int max)
{
    static const vqeaf_wifi_net_t sample[] = {
        { "QEAF-LAB", "02:00:00:00:00:01", 78, 1, VQEAF_WIFI_ENC_WPA2_PSK },
        { "QEAF-GUEST", "02:00:00:00:00:02", 34, 0, VQEAF_WIFI_ENC_OPEN },
    };
    if(out == NULL || max <= 0) return -EINVAL;
    if(!vqeaf_state.wifi_if_present) return -ENODEV;
    int count = (int)(sizeof(sample) / sizeof(sample[0]));
    if(count > max) count = max;
    for(int i = 0; i < count; i++) out[i] = sample[i];
    return count;
}
int vqeaf_wifi_connect(const char *ssid, const char *password, int enc)
{
    (void)ssid; (void)password; (void)enc;
    return -EOPNOTSUPP;
}
int vqeaf_wifi_disconnect(void) { return -EOPNOTSUPP; }
int vqeaf_wifi_get_status(vqeaf_wifi_status_t *status)
{
    if(status == NULL) return -EINVAL;
    memset(status, 0, sizeof(*status));
    snprintf(status->state, sizeof(status->state), "SIMULATED");
    return 0;
}
#endif

const char *vqeaf_wifi_error(int rc)
{
    switch(rc) {
    case -ENODEV: return "Chưa có wlan0 hoặc driver/firmware chưa sẵn sàng.";
    case -ENOENT: return "Dịch vụ WPA chưa chạy hoặc chưa tạo control socket.";
    case -ECONNREFUSED: return "WPA supplicant chưa nhận kết nối điều khiển.";
    case -ETIMEDOUT: return "WPA supplicant không phản hồi.";
    case -EOPNOTSUPP: return "Kiểu bảo mật này chưa được hỗ trợ.";
    case -EINVAL: return "SSID hoặc mật khẩu không hợp lệ.";
    case -EPERM: return "Không đủ quyền điều khiển Wi-Fi.";
    case -ENOTCONN: return "Chưa có kết nối Wi-Fi.";
    default: return "Wi-Fi không thực hiện được yêu cầu.";
    }
}

const char *vqeaf_wifi_enc_name(int enc)
{
    switch(enc) {
    case VQEAF_WIFI_ENC_WEP: return "WEP";
    case VQEAF_WIFI_ENC_WPA: return "WPA-PSK";
    case VQEAF_WIFI_ENC_WPA2_PSK: return "WPA2-PSK";
    case VQEAF_WIFI_ENC_WPA_WPA2: return "WPA/WPA2-PSK";
    case VQEAF_WIFI_ENC_WPA3_SAE: return "WPA3-SAE";
    case VQEAF_WIFI_ENC_OWE: return "OWE";
    case VQEAF_WIFI_ENC_EAP: return "Enterprise (EAP)";
    default: return "Mở";
    }
}

static const char *wifi_ifname(void)
{
    if(vqeaf_state.wifi_ifname[0] != '\0') return vqeaf_state.wifi_ifname;
    return "wlan0";
}
