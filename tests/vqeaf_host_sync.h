/* Fake QEAFRESCUE volume for host harnesses. The list and read hooks share
 * one tree so reported sizes always match the bytes the read hook returns.
 * Paths are absolute ("/", "/Apps", "/Apps/Tools"); dot entries are not
 * listed, matching fat16_list_dir, which skips "." and "..". */
#ifndef VQEAF_HOST_SYNC_H
#define VQEAF_HOST_SYNC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vqeaf_ui.h"

static const struct {
    const char *dir;
    const char *name;
    const char *text;
    uint32_t size;
    uint8_t is_dir;
} host_sync_tree[] = {
    { "/", "BOOTIMG.BIN", NULL, 19587072u, 0 },
    { "/", "DEVINFO.TXT", "VQEAF OS violet\nBuild: host-render\n", 0, 0 },
    { "/", "RESPONSE.TXT", "OK\n", 0, 0 },
    { "/", "SCREEN.RAW", NULL, 249600u, 0 },
    { "/", "Apps", NULL, 0, 1 },
    { "/", "Themes", NULL, 0, 1 },
    { "/Apps", "README.TXT", "Thu muc Apps tren QEAFRESCUE.\n", 0, 0 },
    /* Package bytes for the installer page and the confirm dialog. */
    { "/Apps", "demo.qeapp", "QEAPP demo payload", 0, 0 },
    { "/Apps", "Tools", NULL, 0, 1 },
    { "/Apps/Tools", "TOOL.TXT", "Cong cu mau trong Apps/Tools.\n", 0, 0 },
    { "/Themes", "THEME.TXT", "Chu de mau.\n", 0, 0 },
};

static int host_sync_list(const char *path, vqeaf_sync_entry_t *out, int max)
{
    int count = 0;
    if(path == NULL || out == NULL || max <= 0) return 0;
    for(size_t i = 0;
        i < sizeof(host_sync_tree) / sizeof(host_sync_tree[0]) && count < max;
        i++) {
        if(strcmp(host_sync_tree[i].dir, path) != 0) continue;
        memset(&out[count], 0, sizeof(out[count]));
        snprintf(out[count].name, sizeof(out[count].name), "%s",
                 host_sync_tree[i].name);
        out[count].size = host_sync_tree[i].text != NULL ?
            (uint32_t)strlen(host_sync_tree[i].text) : host_sync_tree[i].size;
        out[count].is_dir = host_sync_tree[i].is_dir;
        count++;
    }
    return count;
}

static int host_sync_read(const char *path, uint8_t **out, size_t *size)
{
    char full[160];
    if(path == NULL || out == NULL || size == NULL) return -1;
    for(size_t i = 0; i < sizeof(host_sync_tree) / sizeof(host_sync_tree[0]);
        i++) {
        if(host_sync_tree[i].text == NULL) continue;
        if(strcmp(host_sync_tree[i].dir, "/") == 0)
            snprintf(full, sizeof(full), "/%s", host_sync_tree[i].name);
        else
            snprintf(full, sizeof(full), "%s/%s", host_sync_tree[i].dir,
                     host_sync_tree[i].name);
        if(strcmp(path, full) != 0) continue;
        size_t len = strlen(host_sync_tree[i].text);
        uint8_t *buffer = malloc(len);
        if(buffer == NULL) return -1;
        memcpy(buffer, host_sync_tree[i].text, len);
        *out = buffer;
        *size = len;
        return 0;
    }
    return -1;
}

#endif