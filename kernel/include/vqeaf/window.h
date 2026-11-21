#ifndef VQEAF_WINDOW_H
#define VQEAF_WINDOW_H
#include <stdint.h>
#include "vqeaf/el0_abi.h"
/* SurfaceFlinger-like window manager for the independent AArch64 kernel.
 *
 * A window is a real layer owned by one EL0 task: it has bounds, a z order, a
 * visibility flag, and a backing buffer mapped into the owner's user root.
 * The client draws into that buffer itself, then calls present() to mark the
 * layer dirty. The compositor recomposes background -> legacy F2c surfaces ->
 * windows in ascending z order with clipping. Focus (keyboard target) is the
 * topmost visible window, exactly like a window manager's focus stack.
 *
 * EMULATED QEMU virt evidence only; this is not a violet panel driver. */
void vqeaf_wm_init(void);

/* Create a window for the calling EL0 task. desc points to validated user
 * memory. Returns the window id (>0) or a negative errno-like code. */
int vqeaf_wm_create(const vqeaf_window_desc_t *desc, int32_t owner);

/* Mark a window's buffer as ready for the next compose. Ownership checked. */
int vqeaf_wm_present(uint32_t id, int32_t owner);

/* Change z order (recomputes focus). Ownership checked. */
int vqeaf_wm_set_z(uint32_t id, int32_t owner, int32_t z);

/* Close one window, or every window belonging to a task that exited. */
int vqeaf_wm_close(uint32_t id, int32_t owner);
void vqeaf_wm_close_owner(int32_t owner);

/* InputFlinger-like delivery: push one key byte to the focused window. */
void vqeaf_input_push(uint8_t key);
/* Pop one event for a window owned by `owner`. 1 = got event, 0 = empty,
 * negative = invalid id/owner/buffer. */
int vqeaf_wm_poll_input(uint32_t id, int32_t owner, vqeaf_key_event_t *out);

/* Compositor/evidence helpers. */
int vqeaf_wm_count(void);
int vqeaf_wm_focused(void);
uint32_t vqeaf_wm_deliveries(void);
/* Bumped on create/present/raise/close/input delivery: the compositor only
 * rebuilds a frame when the layer state actually changed (damage driven). */
uint32_t vqeaf_wm_generation(void);
/* Blit the window stack over an already prepared framebuffer (background +
 * legacy F2c surfaces are the compositor's job). */
void vqeaf_wm_compose(volatile uint32_t *fb);
int vqeaf_wm_frame_done(void);
void vqeaf_wm_set_frame_done(void);

#endif /* VQEAF_WINDOW_H */
