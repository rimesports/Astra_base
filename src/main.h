#pragma once

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim2;
extern UART_HandleTypeDef huart2;

void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_TIM2_Init(void);
void MX_USART2_UART_Init(void);

#ifdef __cplusplus
}
#endif
