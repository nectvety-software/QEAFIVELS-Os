/* Host gate: QEAFRESCUE exposes only the OS storage roots and their real user
 * folders, without vendor clutter or unused duplicate media directories. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/fat16.h"
#include "fs/storage.h"

static int g_fail;

static int has_root(struct fat16_volume *vol, const char *want)
{
    fat16_entry_t entries[128];
    int n = fat16_list_root(vol, entries, 128);
    for(int i = 0; i < n; i++)
        if(strcmp(entries[i].name, want) == 0 && entries[i].is_dir) return 1;
    return 0;
}

static int same_name_ci(const char *a, const char *b)
{
    while(*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if(ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if(cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if(ca != cb) return 0;
    }
    return *a == *b;
}

static int has_root_ci(struct fat16_volume *vol, const char *want)
{
    fat16_entry_t entries[128];
    int n = fat16_list_root(vol, entries, 128);
    for(int i = 0; i < n; i++)
        if(same_name_ci(entries[i].name, want) && entries[i].is_dir) return 1;
    return 0;
}

static int has_path(struct fat16_volume *vol, const char *path,
                    const char *want)
{
    fat16_entry_t entries[128];
    int n = fat16_list_path(vol, path, entries, 128);
    for(int i = 0; i < n; i++)
        if(strcmp(entries[i].name, want) == 0 && entries[i].is_dir) return 1;
    return 0;
}

int main(void)
{
    static const char *const roots[] = { "VQEAF", "UserData", "Install" };
    static const struct {
        const char *path;
        const char *name;
    } user_dirs[] = {
        { "/UserData", "Documents" },
        { "/UserData", "Downloads" },
        { "/UserData/Downloads", "Apps" },
        { "/UserData", "Pictures" },
        { "/UserData", "Music" },
        { "/UserData", "Videos" },
        { "/UserData", "Backups" },
        { "/UserData", "AppData" },
    };
    static const char *const unrelated[] = {
        ".androidsafe", ".applogs", ".camera", ".mcs", ".OTA",
        ".SAU", ".thumbnails", "Android", "apn", "ColorOS",
        "DCIM", "Download", "Kingroot", "kinguserdown", "Movies",
        "Music", "ndownloads", "oppo_log", "Pictures", "Recordings",
        "tencent", "ThemeStore", "toprange", "TouchPal2015",
    };
    uint8_t *image = calloc(FAT16_VOLUME_SIZE, 1);
    struct fat16_volume vol;
    fat16_entry_t entries[128];
    int created;
    int root_count;

    if(!image) return 2;
    if(fat16_init(&vol, image, FAT16_VOLUME_SIZE) != 0) {
        printf("FAIL: fat16_init\n");
        free(image);
        return 1;
    }
    created = storage_publish(&vol);
    printf("storage_publish created=%d (base=%d)\n", created, UMOS_STORAGE_COUNT);
    if(created != UMOS_STORAGE_COUNT) g_fail++;
    root_count = fat16_list_root(&vol, entries, 128);
    printf("root entries=%d (expected=%d) %s\n", root_count,
           UMOS_STORAGE_COUNT,
           root_count == UMOS_STORAGE_COUNT ? "OK" : "UNEXPECTED");
    if(root_count != UMOS_STORAGE_COUNT) g_fail++;
    for(int i = 0; i < root_count; i++) {
        int allowed = 0;
        for(unsigned j = 0; j < sizeof(roots) / sizeof(roots[0]); j++)
            if(same_name_ci(entries[i].name, roots[j])) allowed = 1;
        printf("root %-16s %s\n", entries[i].name,
               allowed && entries[i].is_dir ? "OK" : "UNEXPECTED");
        if(!allowed || !entries[i].is_dir) g_fail++;
    }
    for(unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        int ok = has_root(&vol, roots[i]);
        printf("%-16s %s\n", roots[i], ok ? "OK" : "MISSING");
        if(!ok) g_fail++;
    }
    for(unsigned i = 0; i < sizeof(user_dirs) / sizeof(user_dirs[0]); i++) {
        char path[160];
        int ok = has_path(&vol, user_dirs[i].path, user_dirs[i].name);
        snprintf(path, sizeof(path), "%s/%s", user_dirs[i].path,
                 user_dirs[i].name);
        printf("%-32s %s\n", path, ok ? "OK" : "MISSING");
        if(!ok) g_fail++;
    }
    for(unsigned i = 0; i < sizeof(unrelated) / sizeof(unrelated[0]); i++) {
        int present = has_root_ci(&vol, unrelated[i]);
        printf("%-16s %s\n", unrelated[i], present ? "UNEXPECTED" : "ABSENT");
        if(present) g_fail++;
    }
    free(image);
    printf(g_fail ? "FAILED %d\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
