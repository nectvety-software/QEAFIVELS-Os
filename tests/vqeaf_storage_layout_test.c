/* Host gate: the QEAFRESCUE RAM volume must publish the familiar user folders
 * next to the OS's own VQEAF/UserData/Install directories. */
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

int main(void)
{
    static const char *const dirs[] = {
        ".androidsafe", ".applogs", ".camera", ".mcs", ".OTA",
        ".SAU", ".thumbnails", "Android", "apn", "ColorOS",
        "DCIM", "Download", "Kingroot", "kinguserdown", "Movies",
        "Music", "ndownloads", "oppo_log", "Pictures", "Recordings",
        "tencent", "ThemeStore", "toprange", "TouchPal2015",
        "VQEAF", "UserData", "Install",
    };
    uint8_t *image = calloc(FAT16_VOLUME_SIZE, 1);
    struct fat16_volume vol;
    int created;

    if(!image) return 2;
    if(fat16_init(&vol, image, FAT16_VOLUME_SIZE) != 0) {
        printf("FAIL: fat16_init\n");
        free(image);
        return 1;
    }
    created = storage_publish(&vol);
    printf("storage_publish created=%d (base=%d)\n", created, UMOS_STORAGE_COUNT);
    if(created != UMOS_STORAGE_COUNT) g_fail++;
    for(unsigned i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
        int ok = has_root(&vol, dirs[i]);
        printf("%-16s %s\n", dirs[i], ok ? "OK" : "MISSING");
        if(!ok) g_fail++;
    }
    free(image);
    printf(g_fail ? "FAILED %d\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
