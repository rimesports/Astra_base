#pragma once

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim2;

// Task handles — exposed for T:200 stack watermark reporting
extern TaskHandle_t h_control;
extern TaskHandle_t h_serial;
extern TaskHandle_t h_telemetry;

void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_TIM2_Init(void);

#ifdef __cplusplus
}
#endif
