/* SPDX-License-Identifier: MIT */
/* system/core/storage_layout.c - publish the standard VQEAF storage tree
 * onto a FAT16 volume. Mirrors QeafdropOs fs/storage.c (VQEAF/UserData/
 * Install + nested system/apps/framework/...) adapted to the x86_64 UEFI
 * storage stack.
 */

#include "vqeaf/storage_layout.h"
#include "vqeaf/storage.h"
#include "vqeaf/fs_fat16.h"
#include "vqeaf/serial.h"
#include <string.h>

/* VQEAF subtree (extern definition, matches header declaration). */
const char *const STORAGE_VQEAF_SUB[] = {
    "system", "apps", "framework", "themes", "boot",
    "drivers", "services", "updates", "logs"
};

/* VQEAF/system subtree (used internally for nested publishing). */
static const char *const vqeaf_system_sub[] = {
    "config", "settings", "language", "fonts", "permissions"
};

/* UserData subtree (extern definition). */
const char *const STORAGE_USER_SUB[] = {
    "Documents", "Downloads", "Pictures", "Music", "Videos",
    "Backups", "AppData"
};

/* Install subtree (extern definition). */
const char *const STORAGE_INSTALL_SUB[] = {
    "Apps", "Themes", "Drivers", "Updates"
};

/* Publish a single directory path (creates parents as needed). */
static int publish_path(VqeafFat16Mount *mnt, const char *rel_path) {
    if (!mnt || !rel_path) return -1;
    int r = vqeaf_fat16_mkdir(mnt, rel_path);
    if (r != 0) {
        vqeaf_serial_write("STORAGE: mkdir failed for ");
        vqeaf_serial_write(rel_path);
        vqeaf_serial_write("\r\n");
    }
    return r;
}

/* Publish the standard directory tree onto a FAT16 volume.
 * Returns 0 on success, negative on first error. */
int vqeaf_storage_publish_layout(void *fat_mount) {
    VqeafFat16Mount *mnt = (VqeafFat16Mount *)fat_mount;
    if (!mnt) {
        vqeaf_serial_write("STORAGE: no FAT16 mount provided\r\n");
        return -1;
    }

    /* Top-level roots */
    const char *const *roots[] = { STORAGE_VQEAF_SUB, STORAGE_USER_SUB, STORAGE_INSTALL_SUB };
    const char *root_names[] = { "VQEAF", "UserData", "Install" };
    int root_counts[] = { STORAGE_VQEAF_SUB_COUNT, STORAGE_USER_SUB_COUNT, STORAGE_INSTALL_SUB_COUNT };

    for (int ri = 0; ri < 3; ri++) {
        /* Create the top-level root directory. */
        int r = vqeaf_fat16_mkdir(mnt, root_names[ri]);
        if (r != 0 && r != -2) { /* -2 = already exists, acceptable */
            vqeaf_serial_write("STORAGE: mkdir root failed for ");
            vqeaf_serial_write(root_names[ri]);
            vqeaf_serial_write("\r\n");
            return r;
        }

        /* Create nested subdirectories under each root. */
        for (int si = 0; si < root_counts[ri]; si++) {
            char sub[256];
            int len = 0;
            while (root_names[ri][len]) { sub[len] = root_names[ri][len]; len++; }
            sub[len++] = '/';
            const char *name = roots[ri][si];
            int nl = 0; while (name[nl]) nl++;
            for (int i = 0; i < nl; i++) sub[len + i] = name[i];
            sub[len + nl] = '\0';

            r = publish_path(mnt, sub);
            if (r != 0 && r != -2) return r;
        }

        /* Special: VQEAF/system/{config,settings,language,fonts,permissions} */
        if (strcmp(root_names[ri], "VQEAF") == 0) {
            for (int si = 0; si < 5; si++) {
                char sub[256];
                int len = 0;
                while ("VQEAF/system"[len]) { sub[len] = "VQEAF/system"[len]; len++; }
                sub[len++] = '/';
                const char *name = vqeaf_system_sub[si];
                int nl = 0; while (name[nl]) nl++;
                for (int i = 0; i < nl; i++) sub[len + i] = name[i];
                sub[len + nl] = '\0';

                r = publish_path(mnt, sub);
                if (r != 0 && r != -2) return r;
            }
        }
    }

    vqeaf_serial_write("STORAGE: layout published\r\n");
    return 0;
}
