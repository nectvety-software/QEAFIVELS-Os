/* SPDX-License-Identifier: MIT */
#include <vqeaf/clipboard.h>

#include <string.h>

static char g_clipboard[VQEAF_CLIPBOARD_MAX_BYTES + 1u];
static size_t g_length;
static uint32_t g_generation;

static int valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while(i < n) {
        unsigned char c = s[i++];
        if(c == 0) return 0;
        if(c < 0x80) continue;
        uint32_t cp;
        unsigned need;
        if(c >= 0xC2 && c <= 0xDF) { cp = c & 0x1Fu; need = 1; }
        else if(c >= 0xE0 && c <= 0xEF) { cp = c & 0x0Fu; need = 2; }
        else if(c >= 0xF0 && c <= 0xF4) { cp = c & 0x07u; need = 3; }
        else return 0;
        if(i + need > n) return 0;
        for(unsigned k = 0; k < need; k++) {
            unsigned char d = s[i++];
            if((d & 0xC0u) != 0x80u) return 0;
            cp = (cp << 6) | (d & 0x3Fu);
        }
        if((need == 1 && cp < 0x80u) ||
           (need == 2 && cp < 0x800u) ||
           (need == 3 && cp < 0x10000u) ||
           (cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu)
            return 0;
    }
    return 1;
}

int vqeaf_clipboard_set(const char *utf8, size_t length)
{
    if(utf8 == NULL || length > VQEAF_CLIPBOARD_MAX_BYTES ||
       !valid_utf8((const unsigned char *)utf8, length))
        return -1;
    memcpy(g_clipboard, utf8, length);
    g_clipboard[length] = '\0';
    g_length = length;
    g_generation++;
    return 0;
}

int vqeaf_clipboard_get(char *out, size_t capacity, size_t *length)
{
    if(length != NULL) *length = g_length;
    if(out == NULL || capacity <= g_length) return -1;
    memcpy(out, g_clipboard, g_length + 1u);
    return 0;
}

const char *vqeaf_clipboard_peek(size_t *length)
{
    if(length != NULL) *length = g_length;
    return g_clipboard;
}

int vqeaf_clipboard_has_text(void) { return g_length != 0; }

void vqeaf_clipboard_clear(void)
{
    volatile char *p = g_clipboard;
    for(size_t i = 0; i < sizeof(g_clipboard); i++) p[i] = 0;
    g_length = 0;
    g_generation++;
}

uint32_t vqeaf_clipboard_generation(void) { return g_generation; }
