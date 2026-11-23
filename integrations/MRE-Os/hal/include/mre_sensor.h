#ifndef MRE_SENSOR_H
#define MRE_SENSOR_H
#include "mre_types.h"
typedef enum mre_sensor_kind { MRE_SENSOR_ACCELEROMETER=1, MRE_SENSOR_GYROSCOPE=2, MRE_SENSOR_PROXIMITY=3, MRE_SENSOR_LIGHT=4 } mre_sensor_kind_t;
typedef struct mre_sensor_sample { mre_sensor_kind_t kind; int32_t x, y, z; uint64_t timestamp_ns; } mre_sensor_sample_t;
typedef struct mre_sensor_ops { mre_status_t (*read)(mre_sensor_sample_t *sample); mre_status_t (*set_period_us)(uint32_t period_us); } mre_sensor_ops_t;
#endif
