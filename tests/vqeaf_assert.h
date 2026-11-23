/* SPDX-License-Identifier: MIT */
/* Zig's <assert.h> compiles assert() to a no-op with the flags used by our
 * host runners (-O1, non-Debug). Use this header in C tests so a failed
 * condition actually fails the test. This is test infrastructure only. */
#ifndef VQEAF_ASSERT_H
#define VQEAF_ASSERT_H
#include <stdio.h>
#include <stdlib.h>
#define assert(expr) do {                                                   \
    if(!(expr)) {                                                           \
        fprintf(stderr, "ASSERT FAILED %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        abort();                                                            \
    }                                                                       \
} while(0)
#endif
