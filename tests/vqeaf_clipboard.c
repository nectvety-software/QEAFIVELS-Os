#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vqeaf/clipboard.h>

int main(void)
{
    char out[VQEAF_CLIPBOARD_MAX_BYTES + 1u];
    size_t n = 0;
    assert(!vqeaf_clipboard_has_text());
    assert(vqeaf_clipboard_set("QEAF clipboard", 14) == 0);
    assert(vqeaf_clipboard_has_text());
    assert(vqeaf_clipboard_get(out, sizeof(out), &n) == 0);
    assert(n == 14 && strcmp(out, "QEAF clipboard") == 0);
    assert(vqeaf_clipboard_set("Xin chào 👋", strlen("Xin chào 👋")) == 0);
    assert(vqeaf_clipboard_get(out, sizeof(out), &n) == 0);
    assert(strcmp(out, "Xin chào 👋") == 0);
    assert(vqeaf_clipboard_set("\xC0\xAF", 2) != 0); /* overlong UTF-8 */
    assert(vqeaf_clipboard_set("\xED\xA0\x80", 3) != 0); /* surrogate */
    assert(vqeaf_clipboard_set("x", VQEAF_CLIPBOARD_MAX_BYTES + 1u) != 0);
    assert(vqeaf_clipboard_get(out, 1, &n) != 0);
    assert(n == strlen("Xin chào 👋"));
    uint32_t gen = vqeaf_clipboard_generation();
    vqeaf_clipboard_clear();
    assert(vqeaf_clipboard_generation() == gen + 1u);
    assert(!vqeaf_clipboard_has_text());
    assert(vqeaf_clipboard_get(out, sizeof(out), &n) == 0 && n == 0);
    puts("vqeaf_clipboard: ALL OK");
    return 0;
}
