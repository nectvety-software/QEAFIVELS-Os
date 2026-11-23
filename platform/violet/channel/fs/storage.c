/* SPDX-License-Identifier: MIT */
/* fs/storage.c - the OS's user storage folders. */

#include "storage.h"
#include <string.h>
#include "core/log.h"

static const char *const NAMES[UMOS_STORAGE_COUNT] = {
    "VQEAF", "UserData", "Install"
};

const char *storage_name(int kind) {
    if (kind < 0 || kind >= UMOS_STORAGE_COUNT) return NULL;
    return NAMES[kind];
}

int storage_kind_from_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < UMOS_STORAGE_COUNT; i++)
        if (strcmp(name, NAMES[i]) == 0) return i;
    return -1;
}

/* Empty, RAM-only folders matching the layout a stock violet internal-storage
 * screen shows (OPPO/ColorOS user view). They carry no files and no vendor
 * branding content: this is only a familiar place for user captures. */
static const char *const STANDARD_STORAGE_DIRS[] = {
    ".androidsafe", ".applogs", ".camera", ".mcs", ".OTA",
    ".SAU", ".thumbnails", "Android", "apn", "ColorOS",
    "DCIM", "Download", "Kingroot", "kinguserdown", "Movies",
    "Music", "ndownloads", "oppo_log", "Pictures", "Recordings",
    "tencent", "ThemeStore", "toprange", "TouchPal2015",
};

int storage_publish(struct fat16_volume *vol) {
    int vq, sys, fw, th, df, bt, dr, sv, up, ud, ins;
    unsigned i, published = 0;
    if (!vol) return -1;
    for (i = 0; i < sizeof(STANDARD_STORAGE_DIRS) /
                    sizeof(STANDARD_STORAGE_DIRS[0]); i++) {
        if (fat16_mkdir(vol, STANDARD_STORAGE_DIRS[i]) >= 0) published++;
    }
    umos_logf("storage: published %u standard user folder(s)\n", published);
    vq = fat16_mkdir(vol, "VQEAF");
    if (vq < 0) return -1;
    ud = fat16_mkdir(vol, "UserData");
    if (ud < 0) return -1;
    ins = fat16_mkdir(vol, "Install");
    if (ins < 0) return -1;
    /* VQEAF level */
    sys = fat16_mkdir_in(vol, (uint32_t)vq, "system");
    fat16_mkdir_in(vol, (uint32_t)vq, "apps");
    fw = fat16_mkdir_in(vol, (uint32_t)vq, "framework");
    th = fat16_mkdir_in(vol, (uint32_t)vq, "themes");
    bt = fat16_mkdir_in(vol, (uint32_t)vq, "boot");
    dr = fat16_mkdir_in(vol, (uint32_t)vq, "drivers");
    sv = fat16_mkdir_in(vol, (uint32_t)vq, "services");
    up = fat16_mkdir_in(vol, (uint32_t)vq, "updates");
    fat16_mkdir_in(vol, (uint32_t)vq, "logs");
    fat16_mkdir_in(vol, (uint32_t)vq, "cache");
    if (sys >= 0) {
        fat16_mkdir_in(vol, (uint32_t)sys, "config");
        fat16_mkdir_in(vol, (uint32_t)sys, "settings");
        fat16_mkdir_in(vol, (uint32_t)sys, "language");
        fat16_mkdir_in(vol, (uint32_t)sys, "fonts");
        fat16_mkdir_in(vol, (uint32_t)sys, "permissions");
    }
    if (fw >= 0) {
        fat16_mkdir_in(vol, (uint32_t)fw, "libraries");
        fat16_mkdir_in(vol, (uint32_t)fw, "runtime");
        fat16_mkdir_in(vol, (uint32_t)fw, "api");
        fat16_mkdir_in(vol, (uint32_t)fw, "plugins");
    }
    df = (th >= 0) ? fat16_mkdir_in(vol, (uint32_t)th, "default") : -1;
    if (th >= 0) fat16_mkdir_in(vol, (uint32_t)th, "custom");
    if (df >= 0) {
        fat16_mkdir_in(vol, (uint32_t)df, "wallpaper");
        fat16_mkdir_in(vol, (uint32_t)df, "icons");
        fat16_mkdir_in(vol, (uint32_t)df, "fonts");
        fat16_mkdir_in(vol, (uint32_t)df, "ui");
    }
    if (bt >= 0) {
        fat16_mkdir_in(vol, (uint32_t)bt, "splash");
        fat16_mkdir_in(vol, (uint32_t)bt, "recovery");
    }
    if (dr >= 0) {
        fat16_mkdir_in(vol, (uint32_t)dr, "display");
        fat16_mkdir_in(vol, (uint32_t)dr, "storage");
        fat16_mkdir_in(vol, (uint32_t)dr, "audio");
        fat16_mkdir_in(vol, (uint32_t)dr, "input");
        fat16_mkdir_in(vol, (uint32_t)dr, "network");
    }
    if (sv >= 0) {
        fat16_mkdir_in(vol, (uint32_t)sv, "notification");
        fat16_mkdir_in(vol, (uint32_t)sv, "power");
        fat16_mkdir_in(vol, (uint32_t)sv, "storage");
        fat16_mkdir_in(vol, (uint32_t)sv, "update");
    }
    if (up >= 0) {
        fat16_mkdir_in(vol, (uint32_t)up, "packages");
        fat16_mkdir_in(vol, (uint32_t)up, "cache");
    }
    /* UserData level */
    fat16_mkdir_in(vol, (uint32_t)ud, "Documents");
    {
        int dl = fat16_mkdir_in(vol, (uint32_t)ud, "Downloads");
        /* Packages can be dropped three levels deep (UserData/Downloads/Apps);
         * create it at boot so it exists before the PC mounts the volume. */
        if (dl >= 0) fat16_mkdir_in(vol, (uint32_t)dl, "Apps");
    }
    fat16_mkdir_in(vol, (uint32_t)ud, "Pictures");
    fat16_mkdir_in(vol, (uint32_t)ud, "Music");
    fat16_mkdir_in(vol, (uint32_t)ud, "Videos");
    fat16_mkdir_in(vol, (uint32_t)ud, "Backups");
    fat16_mkdir_in(vol, (uint32_t)ud, "AppData");
    /* Install level */
    fat16_mkdir_in(vol, (uint32_t)ins, "Apps");
    fat16_mkdir_in(vol, (uint32_t)ins, "Themes");
    fat16_mkdir_in(vol, (uint32_t)ins, "Drivers");
    fat16_mkdir_in(vol, (uint32_t)ins, "Updates");
    umos_logf("storage: VQEAF/UserData/Install + %u standard folders published\n",
             published);
    return 3;
}
