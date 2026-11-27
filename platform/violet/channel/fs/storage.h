/* SPDX-License-Identifier: MIT */
/* fs/storage.h - the folders user files live in.
 *
 * One table, two users: the shell's file screens list these, and the
 * QEAFRESCUE volume is built with exactly these directories in its root, so
 * what a host sees over USB is what the phone calls its storage.
 */

#ifndef UMOS_STORAGE_H
#define UMOS_STORAGE_H

#include "fat16.h"

#define UMOS_STORAGE_COUNT 3

enum umos_storage_kind {
    UMOS_STORAGE_VQEAF = 0,
    UMOS_STORAGE_USERDATA,
    UMOS_STORAGE_INSTALL
};

/* Display name as it should appear on the glass and in Explorer. NULL for an
 * out-of-range kind. */
const char *storage_name(int kind);

/* -1 when the name is not one of the three OS storage roots. */
int storage_kind_from_name(const char *name);

/* Publishes VQEAF, UserData and Install into a freshly initialised volume,
 * including the user-media folders below UserData. Returns UMOS_STORAGE_COUNT,
 * or -1 if a root directory could not be created. */
int storage_publish(struct fat16_volume *vol);

#endif
