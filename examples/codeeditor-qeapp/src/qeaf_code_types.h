/* qeaf_code_types.h - constants shared by the CodeEditor QEAPP port.
 *
 * Values mirror the MRE original (src/code_store.h, src/retro_ui.h) so the
 * ported logic keeps the same limits and syntax roles.
 */
#ifndef QEAF_CODE_TYPES_H
#define QEAF_CODE_TYPES_H

#define QEAF_CODE_LEN      1280
#define QEAF_DOC_MAX       14
#define QEAF_NAME_MAX      32
#define QEAF_BASE_MAX      8
#define QEAF_PATH_MAX      64
#define QEAF_UNDO_DEPTH    5
#define QEAF_CLIP_MAX      QEAF_CODE_LEN

/* Syntax roles (retro_ui.h HL_*). */
#define QEAF_HL_PLAIN 0
#define QEAF_HL_KEY   1
#define QEAF_HL_STR   2
#define QEAF_HL_CMT   3
#define QEAF_HL_NUM   4
#define QEAF_HL_PRE   5
#define QEAF_HL_TAG   6
#define QEAF_HL_ATTR  7
#define QEAF_HL_PROP  8
#define QEAF_HL_N     9

/* Languages (retro_ui.h LANG_*). */
#define QEAF_LANG_NONE 0
#define QEAF_LANG_C    1
#define QEAF_LANG_HTML 2
#define QEAF_LANG_CSS  3
#define QEAF_LANG_JS   4
#define QEAF_LANG_PY   5

/* File extensions, frozen order from code_store.h. */
#define QEAF_EXT_C    0
#define QEAF_EXT_H    1
#define QEAF_EXT_TXT  2
#define QEAF_EXT_HTML 3
#define QEAF_EXT_CSS  4
#define QEAF_EXT_JS   5
#define QEAF_EXT_PY   6
#define QEAF_EXT_N    7

#endif /* QEAF_CODE_TYPES_H */
