#include "qeaf_code_hl.h"
#include "qeaf_code_util.h"

#include <string.h>

static const char c_keywords[][24] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned",
    "void", "volatile", "while",
    ""
};

static const char js_keywords[][24] = {
    "async", "await", "break", "case", "catch", "class", "const", "continue",
    "debugger", "default", "delete", "do", "else", "export", "extends",
    "finally", "for", "function", "get", "if", "import", "in", "instanceof",
    "let", "new", "of", "return", "set", "static", "super", "switch", "this",
    "throw", "try", "typeof", "var", "void", "while", "yield",
    "false", "null", "true", "undefined",
    ""
};

static const char js_builtins[][24] = {
    "Array", "Boolean", "Date", "JSON", "Map", "Math", "Number", "Object",
    "Promise", "RegExp", "Set", "String", "Symbol", "console", "document",
    "fetch", "localStorage", "navigator", "parseFloat", "parseInt",
    "sessionStorage", "setTimeout", "setInterval", "window",
    ""
};

static const char py_keywords[][24] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue",
    "def", "del", "elif", "else", "except", "finally", "for", "from",
    "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or",
    "pass", "raise", "return", "try", "while", "with", "yield",
    "False", "None", "True",
    ""
};

static const char py_builtins[][24] = {
    "abs", "all", "any", "bool", "bytes", "callable", "chr", "classmethod",
    "dict", "dir", "enumerate", "filter", "float", "format", "getattr",
    "hasattr", "hash", "hex", "id", "input", "int", "isinstance", "issubclass",
    "iter", "len", "list", "map", "max", "min", "next", "object", "oct",
    "open", "ord", "pow", "print", "property", "range", "repr", "reversed",
    "round", "set", "setattr", "sorted", "staticmethod", "str", "sum",
    "super", "tuple", "type", "vars", "zip",
    "Exception", "ValueError", "TypeError", "KeyError", "IndexError",
    "OSError", "RuntimeError", "StopIteration", "ZeroDivisionError",
    "self", "cls",
    ""
};

static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static void hl_range(uint8_t *roles, int a, int b, uint8_t role)
{
    while(a < b) roles[a++] = role;
}

static int kw_in(const char list[][24], const char *s, int n)
{
    int i;
    for(i = 0; list[i][0] != '\0'; i++)
        if(qeaf_word_at(s, n, list[i])) return 1;
    return 0;
}

static int iword_at(const char *s, int n, const char *word)
{
    int i = 0;
    while(word[i] != '\0') {
        char a, b;
        if(i >= n) return 0;
        a = s[i];
        b = word[i];
        if(a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if(b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if(a != b) return 0;
        i++;
    }
    if(i < n && qeaf_is_ident_char(s[i])) return 0;
    return 1;
}

static int is_call(const char *s, int len, int k)
{
    while(k < len && (s[k] == ' ' || s[k] == '\t')) k++;
    return k < len && s[k] == '(';
}

void qeaf_hl_tokenize(const char *s, int lang, uint8_t *roles, int cap)
{
    int i = 0, len;
    int mode = lang;
    int at_line_start = 1;
    int in_tag = 0;
    int css_block = 0;
    int pend = -1;

    if(s == NULL || roles == NULL || cap <= 0) return;
    len = qeaf_strlen(s);
    if(len > cap - 1) len = cap - 1;
    memset(roles, QEAF_HL_PLAIN, (size_t)len);
    if(mode == QEAF_LANG_NONE) return;

    while(i < len) {
        char c = s[i];

        if(c == '\n') {
            at_line_start = 1;
            if(mode == QEAF_LANG_HTML) in_tag = 0;
            i++;
            continue;
        }
        if(c == ' ' || c == '\t' || c == '\r') { i++; continue; }

        /* ---- HTML markup --------------------------------------------- */
        if(mode == QEAF_LANG_HTML && in_tag) {
            if(c == '>') {
                roles[i] = QEAF_HL_TAG;
                i++;
                in_tag = 0;
                if(pend >= 0) { mode = pend; pend = -1; }
                continue;
            }
            if(c == '"' || c == '\'') {
                int j = i + 1;
                roles[i] = QEAF_HL_STR;
                while(j < len && s[j] != c && s[j] != '\n') roles[j++] = QEAF_HL_STR;
                if(j < len && s[j] == c) roles[j++] = QEAF_HL_STR;
                i = j;
                continue;
            }
            if(qeaf_is_ident_head(c)) {
                int k = i;
                while(k < len && (qeaf_is_ident_char(s[k]) || s[k] == '-')) k++;
                hl_range(roles, i, k, QEAF_HL_ATTR);
                i = k;
                continue;
            }
            i++;
            continue;
        }

        if(mode == QEAF_LANG_HTML && c == '<') {
            if(i + 2 < len && s[i + 1] == '!' && s[i + 2] == '-' &&
               i + 3 < len && s[i + 3] == '-') {
                int j = i + 4, end;
                while(j < len && !(s[j] == '-' && j + 2 < len &&
                                   s[j + 1] == '-' && s[j + 2] == '>')) {
                    j++;
                }
                end = (j + 3 < len) ? j + 3 : len;
                hl_range(roles, i, end, QEAF_HL_CMT);
                i = end;
                continue;
            }
            if(i + 1 < len && s[i + 1] == '!') {
                int j = i;
                while(j < len && s[j] != '>' && s[j] != '\n')
                    roles[j++] = QEAF_HL_PRE;
                if(j < len && s[j] == '>') roles[j++] = QEAF_HL_PRE;
                i = j;
                continue;
            }
            roles[i++] = QEAF_HL_TAG;
            if(i < len && s[i] == '/') roles[i++] = QEAF_HL_TAG;
            {
                int k = i;
                while(k < len && (qeaf_is_ident_char(s[k]) || s[k] == '-')) k++;
                if(k > i) {
                    hl_range(roles, i, k, QEAF_HL_TAG);
                    if(iword_at(s + i, k - i, "script")) pend = QEAF_LANG_JS;
                    else if(iword_at(s + i, k - i, "style")) pend = QEAF_LANG_CSS;
                    i = k;
                }
            }
            in_tag = 1;
            at_line_start = 0;
            continue;
        }

        if((mode == QEAF_LANG_JS || mode == QEAF_LANG_CSS) && c == '<' &&
           i + 1 < len && s[i + 1] == '/') {
            const char *want = mode == QEAF_LANG_JS ? "script" : "style";
            int n = qeaf_strlen(want);
            if(iword_at(s + i + 2, n, want)) {
                hl_range(roles, i, i + 2 + n, QEAF_HL_TAG);
                mode = QEAF_LANG_HTML;
                in_tag = 1;
                i += 2 + n;
                continue;
            }
        }

        /* ---- comments ------------------------------------------------- */
        if(c == '/' && i + 1 < len && s[i + 1] == '*' &&
           (mode == QEAF_LANG_C || mode == QEAF_LANG_CSS ||
            mode == QEAF_LANG_JS)) {
            int j = i + 2;
            roles[i] = roles[i + 1] = QEAF_HL_CMT;
            while(j < len) {
                roles[j] = QEAF_HL_CMT;
                if(s[j] == '*' && j + 1 < len && s[j + 1] == '/') {
                    roles[j + 1] = QEAF_HL_CMT;
                    j += 2;
                    break;
                }
                j++;
            }
            at_line_start = 0;
            i = j;
            continue;
        }
        if(c == '/' && i + 1 < len && s[i + 1] == '/' &&
           (mode == QEAF_LANG_C || mode == QEAF_LANG_JS)) {
            int j = i;
            while(j < len && s[j] != '\n') roles[j++] = QEAF_HL_CMT;
            at_line_start = 0;
            i = j;
            continue;
        }

        /* ---- Python --------------------------------------------------- */
        if(mode == QEAF_LANG_PY && c == '#') {
            int j = i;
            while(j < len && s[j] != '\n') roles[j++] = QEAF_HL_CMT;
            at_line_start = 0;
            i = j;
            continue;
        }
        if(mode == QEAF_LANG_PY && c == '@' && at_line_start) {
            int j = i + 1;
            roles[i] = QEAF_HL_PRE;
            while(j < len && (qeaf_is_ident_char(s[j]) || s[j] == '.')) {
                roles[j] = QEAF_HL_PRE;
                j++;
            }
            at_line_start = 0;
            i = j;
            continue;
        }

        /* ---- whole-line directives ------------------------------------ */
        if(mode == QEAF_LANG_C && c == '#' && at_line_start) {
            int j = i;
            while(j < len && s[j] != '\n') {
                if(s[j] == '/' && j + 1 < len && s[j + 1] == '/') {
                    while(j < len && s[j] != '\n') roles[j++] = QEAF_HL_CMT;
                    break;
                }
                roles[j++] = QEAF_HL_PRE;
            }
            at_line_start = 0;
            i = j;
            continue;
        }
        if(mode == QEAF_LANG_CSS && c == '@' && at_line_start) {
            int j = i;
            while(j < len && s[j] != '{' && s[j] != ';' && s[j] != '\n')
                roles[j++] = QEAF_HL_PRE;
            at_line_start = 0;
            css_block = 0;
            i = j;
            continue;
        }

        /* ---- strings -------------------------------------------------- */
        if(c == '"' || c == '\'' || (c == '`' && mode == QEAF_LANG_JS)) {
            int j = i + 1;
            roles[i] = QEAF_HL_STR;
            if(mode == QEAF_LANG_PY && i + 2 < len && s[i + 1] == c &&
               s[i + 2] == c) {
                roles[i + 1] = roles[i + 2] = QEAF_HL_STR;
                j = i + 3;
                at_line_start = 0;
                while(j < len) {
                    if(s[j] == c && j + 2 < len && s[j + 1] == c &&
                       s[j + 2] == c) {
                        roles[j] = roles[j + 1] = roles[j + 2] = QEAF_HL_STR;
                        j += 3;
                        at_line_start = 0;
                        break;
                    }
                    roles[j] = QEAF_HL_STR;
                    at_line_start = (s[j] == '\n');
                    j++;
                }
                i = j;
                continue;
            }
            while(j < len && s[j] != c && s[j] != '\n') {
                roles[j++] = QEAF_HL_STR;
                if(s[j - 1] == '\\' && j < len) roles[j++] = QEAF_HL_STR;
            }
            if(j < len && s[j] == c) { roles[j] = QEAF_HL_STR; j++; }
            at_line_start = 0;
            i = j;
            continue;
        }

        /* ---- CSS colour literal -------------------------------------- */
        if(mode == QEAF_LANG_CSS && c == '#') {
            int k = i + 1, n = 0;
            while(k < len && is_hex_digit(s[k])) { n++; k++; }
            if(n == 3 || n == 6) { hl_range(roles, i, k, QEAF_HL_NUM); i = k; continue; }
        }

        /* ---- HTML entity --------------------------------------------- */
        if(mode == QEAF_LANG_HTML && c == '&') {
            int k = i + 1;
            while(k < len && qeaf_is_ident_char(s[k])) k++;
            if(k > i + 1 && k < len && s[k] == ';') {
                hl_range(roles, i, k + 1, QEAF_HL_NUM);
                i = k + 1;
                continue;
            }
        }

        /* ---- numbers -------------------------------------------------- */
        if(c >= '0' && c <= '9') {
            int j = i;
            if(c == '0' && j + 1 < len &&
               (s[j + 1] == 'x' || s[j + 1] == 'X' ||
                (mode == QEAF_LANG_PY && (s[j + 1] == 'b' || s[j + 1] == 'B' ||
                                          s[j + 1] == 'o' || s[j + 1] == 'O'))) &&
               (mode == QEAF_LANG_C || mode == QEAF_LANG_JS ||
                mode == QEAF_LANG_PY)) {
                roles[j++] = QEAF_HL_NUM;
                roles[j++] = QEAF_HL_NUM;
                while(j < len && is_hex_digit(s[j])) { roles[j] = QEAF_HL_NUM; j++; }
            } else {
                while(j < len && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.')) {
                    roles[j] = QEAF_HL_NUM;
                    j++;
                }
            }
            while(j < len && qeaf_is_ident_char(s[j])) { roles[j] = QEAF_HL_NUM; j++; }
            at_line_start = 0;
            i = j;
            continue;
        }

        /* ---- identifiers ---------------------------------------------- */
        if(qeaf_is_ident_head(c) ||
           (mode == QEAF_LANG_CSS && (c == '.' || c == '-'))) {
            int k = i + (mode == QEAF_LANG_CSS && c == '.' ? 1 : 0);
            while(k < len && (qeaf_is_ident_char(s[k]) || s[k] == '-' ||
                              s[k] == '$')) k++;
            if(k > i) {
                int n = k - i;
                if(mode == QEAF_LANG_C) {
                    if(kw_in(c_keywords, s + i, n)) hl_range(roles, i, k, QEAF_HL_KEY);
                } else if(mode == QEAF_LANG_JS) {
                    if(kw_in(js_keywords, s + i, n)) hl_range(roles, i, k, QEAF_HL_KEY);
                    else if(kw_in(js_builtins, s + i, n) || is_call(s, len, k))
                        hl_range(roles, i, k, QEAF_HL_PROP);
                } else if(mode == QEAF_LANG_PY) {
                    if(kw_in(py_keywords, s + i, n)) hl_range(roles, i, k, QEAF_HL_KEY);
                    else if(kw_in(py_builtins, s + i, n) || is_call(s, len, k))
                        hl_range(roles, i, k, QEAF_HL_PROP);
                } else if(mode == QEAF_LANG_CSS) {
                    int p = k;
                    while(p < len && (s[p] == ' ' || s[p] == '\t')) p++;
                    if(css_block && p < len && s[p] == ':')
                        hl_range(roles, i, k, QEAF_HL_PROP);
                    else
                        hl_range(roles, i, k, QEAF_HL_TAG);
                }
                i = k;
                at_line_start = 0;
                continue;
            }
        }

        if(mode == QEAF_LANG_CSS) {
            if(c == '{') css_block = 1;
            else if(c == '}') css_block = 0;
        }
        at_line_start = 0;
        i++;
    }
}
