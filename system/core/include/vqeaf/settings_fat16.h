/* SPDX-License-Identifier: MIT */
/* system/core/include/vqeaf/settings_fat16.h - durable Settings Service
 * backend over the writable FAT16 stack. */
#ifndef VQEAF_SETTINGS_FAT16_H
#define VQEAF_SETTINGS_FAT16_H

#include "vqeaf/fs_fat16.h"

/* Attach the Settings Service to a mounted FAT16 volume. Preferences are kept
 * in a fixed-capacity root file (PREFS.DAT) so every write is an in-place
 * overwrite the FAT16 writer supports. Returns the attach result: 0 when the
 * volume is usable for persistence, negative otherwise. */
int vqeaf_settings_attach_fat16(VqeafFat16Mount *mnt);

#endif /* VQEAF_SETTINGS_FAT16_H */