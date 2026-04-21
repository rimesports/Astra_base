#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void motor_ctrl_init(void);
void motor_ctrl_set_speed(int16_t left, int16_t right);

#ifdef __cplusplus
}
#endif
