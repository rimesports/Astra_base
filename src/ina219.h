#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ina219_init(void);
bool ina219_read(float *voltage, float *current);

#ifdef __cplusplus
}
#endif
