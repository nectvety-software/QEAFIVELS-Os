#ifndef VQEAF_TEXT_H
#define VQEAF_TEXT_H
#include <stdint.h>
/* Locale/Text v0.3: strict UTF-8 scanning, Vietnamese grapheme composition,
 * glyph fallback chains and advance-width measurement. Everything is pure
 * data + functions so it is unit-testable without a renderer. */
#define VQEAF_TEXT_REPLACEMENT 0xFFFDu
/* Returns bytes consumed (1..4) and the codepoint; 0 on malformed input. */
int vqeaf_utf8_next(const char *s,uint32_t *cp);
/* 1 if cp is a combining mark (Mn), covering the Vietnamese-relevant ranges. */
int vqeaf_text_is_combining(uint32_t cp);
/* Vietnamese precomposition: base + mark -> single precomposed codepoint, or 0. */
uint32_t vqeaf_text_compose(uint32_t base,uint32_t mark);
/* Scan one grapheme: base codepoint plus following combining marks; writes the
 * composed codepoint when a Vietnamese precomposition exists (marked via flag),
 * the total UTF-8 length of the grapheme, and the number of combining marks. */
int vqeaf_text_next_grapheme(const char *s,uint32_t *cp,int *composed,int *len,int *marks);
/* ASCII base letter after stripping Vietnamese diacritics; 0 if not mappable. */
uint32_t vqeaf_text_base_letter(uint32_t cp);
/* Glyph fallback: returns the codepoint actually renderable given a font that
 * covers ASCII + Vietnamese Latin Extended Additional. Unknown glyphs fall
 * back to the base letter, then to U+FFFD. */
uint32_t vqeaf_text_glyph_fallback(uint32_t cp);
/* Advance width in 1/1000 em for a glyph after fallback; combining marks are 0. */
uint32_t vqeaf_text_advance(uint32_t cp);
/* Measures a whole UTF-8 string: width, grapheme count; returns 0 if valid. */
int vqeaf_text_measure(const char *utf8,uint32_t *width,uint32_t *graphemes);
#endif
