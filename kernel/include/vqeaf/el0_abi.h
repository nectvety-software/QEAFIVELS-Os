#ifndef VQEAF_EL0_ABI_H
#define VQEAF_EL0_ABI_H
#include <stdint.h>
/* Shared EL0 <-> EL1 ABI for the independent AArch64 kernel (F3).
 *
 * This header is the SINGLE source of truth for syscall numbers and the
 * structs passed across the SVC boundary. Both the kernel (irq.c/window.c)
 * and the EL0 programs (user/vqeaf_el0.h -> launcher, .qen apps) include it.
 * Keep every field uint32_t/uint64_t with explicit padding so AAPCS64 layout
 * is identical on both sides of the exception boundary. */
#define VQEAF_SYS_PUTCHAR       1u  /* x0 = char                            */
#define VQEAF_SYS_PRINT         2u  /* x0 = user string VA, x1 = len       */
#define VQEAF_SYS_EXIT          3u  /* never returns                        */
#define VQEAF_SYS_SEND          4u  /* F1 Binder-like IPC                   */
#define VQEAF_SYS_RECV          5u  /* F1 Binder-like IPC                   */
#define VQEAF_SYS_SUBMIT_SURFACE 6u /* F2c legacy surface submission        */
#define VQEAF_SYS_POLL_INPUT    7u  /* x0 = win id, x1 = event VA           */
#define VQEAF_SYS_WINDOW_CREATE 8u  /* x0 = desc VA, x1 = sizeof(desc)      */
#define VQEAF_SYS_WINDOW_PRESENT 9u /* x0 = win id                          */
#define VQEAF_SYS_WINDOW_SET_Z 10u  /* x0 = win id, x1 = z                  */
#define VQEAF_SYS_LAUNCH       11u  /* x0 = package name VA (NUL)           */
#define VQEAF_SYS_WINDOW_CLOSE 12u  /* x0 = win id                          */
#define VQEAF_SYS_FRAME_DONE   13u  /* x0 = 0 -> 1 once F3 evidence printed */
#define VQEAF_SYS_OPEN         14u  /* x0 = path VA, x1 = VQEAF_O_* flags   */
#define VQEAF_SYS_READ         15u  /* x0 = handle, x1 = buf VA, x2 = len   */
#define VQEAF_SYS_WRITE        16u  /* x0 = handle, x1 = buf VA, x2 = len   */
#define VQEAF_SYS_CLOSE        17u  /* x0 = handle                          */

/* Open flags (shared with the kernel VFS: vqeaf/vfs.h aliases these). */
#define VQEAF_O_RDONLY 1u
#define VQEAF_O_WRONLY 2u
#define VQEAF_O_RDWR   3u

/* Negative syscall results. Distinct from the informational -1 that IPC uses
 * for "no message pending", so a denial is never mistaken for empty data. */
#define VQEAF_EACCES (-13)
#define VQEAF_EBADF  (-9)
#define VQEAF_EINVAL (-22)
#define VQEAF_ENOENT (-2)
#define VQEAF_ENOSPC (-28)
#define VQEAF_EMFILE (-24)

/* Window creation descriptor (32 bytes). buffer_va must be page aligned and
 * inside the per-task user region; the kernel allocates the backing store and
 * maps it EL0-accessible at that address. */
typedef struct {
    uint32_t x, y, w, h;
    int32_t  z;
    uint32_t flags;
    uint64_t buffer_va;
} vqeaf_window_desc_t;

#define VQEAF_WINDOW_DESC_SIZE 32u
#define VQEAF_WINDOW_FLAG_VISIBLE 1u
#define VQEAF_WINDOW_MAX_BYTES (256u * 1024u)
#define VQEAF_WINDOW_MAX_COUNT 8u
#define VQEAF_INPUT_QUEUE 8u

typedef struct {
    uint32_t type;      /* 1 = key press */
    uint32_t key;       /* byte from the PL011 RX FIFO */
    uint32_t mods;      /* reserved, 0 */
    uint32_t seq;       /* monotonically increasing delivery number */
} vqeaf_key_event_t;

#define VQEAF_KEY_EVENT_SIZE 16u
#define VQEAF_KEY_EVENT_PRESS 1u

#endif /* VQEAF_EL0_ABI_H */
