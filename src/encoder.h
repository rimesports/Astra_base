#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void encoder_init(void);
void encoder_exti_callback(uint16_t GPIO_Pin);
void encoder_update(void);
float encoder_get_left_rpm(void);
float encoder_get_right_rpm(void);

#ifdef __cplusplus
}
#endif
