#ifndef QEAF_VERSION_H
#define QEAF_VERSION_H

/* Single source of truth for the OS identity on the violet port. Every
 * user-visible surface (rescue banner, USB descriptors, DEVINFO.TXT, boot
 * log) builds its name from these macros. Official names come from
 * .agents/prompts/QEAFIVELS_OS_MASTER_PROMPT.md: the OS is QEAFIVELS OS;
 * the platform layer is derived from QeafdropOs, the OS is this repo. The
 * x86_64 reference still carries "VQEAF OS" in system/core/include/vqeaf/
 * version.h until its own rebrand sweep. */
#define QEAF_OS_NAME    "QEAFIVELS OS"
#define QEAF_OS_VERSION "0.3.1"
#define QEAF_OS_STRING  QEAF_OS_NAME " " QEAF_OS_VERSION

#endif
