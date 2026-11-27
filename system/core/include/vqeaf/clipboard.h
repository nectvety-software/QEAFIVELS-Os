/* SPDX-License-Identifier: MIT */
#ifndef VQEAF_CLIPBOARD_H
#define VQEAF_CLIPBOARD_H

#include <stddef.h>
#include <stdint.h>

/* Small, process-shared UTF-8 clipboard. Data is volatile and never persisted. */
#define VQEAF_CLIPBOARD_MAX_BYTES 4096u

int vqeaf_clipboard_set(const char *utf8, size_t length);
int vqeaf_clipboard_get(char *out, size_t capacity, size_t *length);
const char *vqeaf_clipboard_peek(size_t *length);
int vqeaf_clipboard_has_text(void);
void vqeaf_clipboard_clear(void);
uint32_t vqeaf_clipboard_generation(void);

#endif
