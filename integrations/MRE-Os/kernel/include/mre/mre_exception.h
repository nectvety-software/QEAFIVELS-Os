#ifndef MRE_EXCEPTION_H
#define MRE_EXCEPTION_H
#include "mre_types.h"
typedef enum mre_exception_vector {
    MRE_VECTOR_SYNC = 0, MRE_VECTOR_IRQ = 1,
    MRE_VECTOR_FIQ = 2, MRE_VECTOR_SERROR = 3
} mre_exception_vector_t;
typedef struct mre_exception_frame {
    uint64_t x[31];
    uint64_t sp_el0, elr, spsr, esr, far;
    uint64_t vector;
} mre_exception_frame_t;
void mre_exception_dispatch(mre_exception_frame_t *frame);
void mre_exception_set_fatal_hook(void (*hook)(const mre_exception_frame_t *));
void mre_panic(const char *reason);
#endif
