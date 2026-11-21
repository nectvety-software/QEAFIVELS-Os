/* qeaf_code_hl.h - syntax tokenizer ported from src/code_hl.h. */
#ifndef QEAF_CODE_HL_H
#define QEAF_CODE_HL_H

#include <stdint.h>

#include "qeaf_code_types.h"

/* Fills roles[0..len) with QEAF_HL_* values for `lang`. Never allocates and
 * never modifies the text. */
void qeaf_hl_tokenize(const char *text, int lang, uint8_t *roles, int cap);

#endif /* QEAF_CODE_HL_H */
