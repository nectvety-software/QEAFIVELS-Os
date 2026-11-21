#ifndef VQEAF_RESCUE_H
#define VQEAF_RESCUE_H

#include <stdint.h>
#include "vqeaf/storage.h"

/* Rescue command channel.
 *
 * The device scans a FAT16 storage volume for a command file (QEAF.CMD by
 * default), executes the requested verb, and writes a response file
 * (RESPONSE.TXT) back to the same volume. Host tools can therefore drive the
 * OS by writing commands into the virtual disk image and reading responses
 * out, without needing a serial connection or keyboard.
 *
 * Safe verbs only: status, version, echo, ls, sync, help. No privileged
 * partition writes are exposed on the x86_64 UEFI reference.
 */

#define RESCUE_CMD_FILE   "QEAF.CMD"
#define RESPONSE_FILE     "RESPONSE.TXT"
#define DEVINFO_FILE      "DEVINFO.TXT"
#define RESCUE_VOLUME     "/storage"   /* FAT16 mount where commands live */

/* Result codes returned by the rescue channel. */
#define RESCUE_OK          0
#define RESCUE_NO_COMMAND  -1
#define RESCUE_BAD_VERB    -2
#define RESCUE_IO_ERROR    -3
#define RESCUE_BUSY        -4

/* Scan all mounted FAT16 volumes for a command file. If found, execute it
 * and write RESPONSE.TXT next to it. Returns RESCUE_OK if a command was
 * processed, RESCUE_NO_COMMAND if none was found. */
int vqeaf_rescue_poll(void);

/* Write a response file to the same FAT16 volume as the command file.
 * Returns RESCUE_OK on success. */
int vqeaf_rescue_respond(const char *volume_path, const char *cmd_file,
                         const char *response);

/* Print the OS identity line (version, build, target) to serial. */
void vqeaf_rescue_print_identity(void);

#endif /* VQEAF_RESCUE_H */
