/* qeaf_code_platform.h - host/OS boundary for the CodeEditor QEAPP port.
 *
 * The MRE build drew through coremre VxpRender2D and received VM key/pen
 * events. This port draws through these callbacks and receives keys/chars from
 * the QEAF app runtime. A future adapter maps them to qeaf_app_api_v2.
 */
#ifndef QEAF_CODE_PLATFORM_H
#define QEAF_CODE_PLATFORM_H

/* Chrome roles. Syntax roles use the QEAF_HL_* values from qeaf_code_types.h;
 * these start above them so one integer carries both. */
#define QEAF_UI_TEXT   0
#define QEAF_UI_TITLE  20
#define QEAF_UI_DIM    21
#define QEAF_UI_ACCENT 22
#define QEAF_UI_SEL    23
#define QEAF_UI_WARN   24
#define QEAF_UI_CARET  25

typedef struct {
    void (*clear)(void *ctx);
    void (*text)(void *ctx, int x, int y, const char *utf8, int role);
    void (*rect)(void *ctx, int x, int y, int w, int h, int role);
    void (*flush)(void *ctx);
    int  (*measure)(void *ctx, const char *utf8);
} qeaf_code_ui_t;

/* Logical keys. Printable input arrives through qeaf_code_app_text(). */
typedef enum {
    QEAF_KEY_UP = 1,
    QEAF_KEY_DOWN,
    QEAF_KEY_LEFT,
    QEAF_KEY_RIGHT,
    QEAF_KEY_OK,
    QEAF_KEY_BACK,
    QEAF_KEY_SOFT_LEFT,
    QEAF_KEY_SOFT_RIGHT,
    QEAF_KEY_CLEAR,
    QEAF_KEY_MENU
} qeaf_code_key_t;

#endif /* QEAF_CODE_PLATFORM_H */
