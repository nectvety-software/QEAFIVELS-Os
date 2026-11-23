#ifndef MRE_INPUT_H
#define MRE_INPUT_H
#include "mre_types.h"
typedef enum mre_input_type { MRE_INPUT_TOUCH=1, MRE_INPUT_BUTTON=2, MRE_INPUT_KEY=3 } mre_input_type_t;
typedef struct mre_input_event { mre_input_type_t type; int32_t code; int32_t x, y; uint32_t value; uint64_t timestamp_ns; } mre_input_event_t;
typedef struct mre_input_ops { mre_status_t (*poll)(mre_input_event_t *event); } mre_input_ops_t;
#endif
