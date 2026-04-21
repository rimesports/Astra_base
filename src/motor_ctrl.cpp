#include "motor_ctrl.h"
#include "astra_config.h"
#include "stm32l4xx_hal.h"
#include <stdlib.h>   // abs()

extern TIM_HandleTypeDef htim2;

static void motor_set_direction(GPIO_TypeDef *port, uint16_t pin, int16_t speed) {
  if (speed >= 0) {
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
  } else {
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
  }
}

void motor_ctrl_init(void) {
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
}

void motor_ctrl_set_speed(int16_t left, int16_t right) {
  int16_t left_pwm = left;
  int16_t right_pwm = right;

  if (left_pwm > MOTOR_PWM_MAX_SPEED) left_pwm = MOTOR_PWM_MAX_SPEED;
  if (left_pwm < -MOTOR_PWM_MAX_SPEED) left_pwm = -MOTOR_PWM_MAX_SPEED;
  if (right_pwm > MOTOR_PWM_MAX_SPEED) right_pwm = MOTOR_PWM_MAX_SPEED;
  if (right_pwm < -MOTOR_PWM_MAX_SPEED) right_pwm = -MOTOR_PWM_MAX_SPEED;

  motor_set_direction(MOTOR_LEFT_DIR_PORT, MOTOR_LEFT_DIR_PIN, left_pwm);
  motor_set_direction(MOTOR_RIGHT_DIR_PORT, MOTOR_RIGHT_DIR_PIN, right_pwm);

  uint32_t left_duty = (uint32_t)((abs(left_pwm) * MOTOR_PWM_MAX) / MOTOR_PWM_MAX_SPEED);
  uint32_t right_duty = (uint32_t)((abs(right_pwm) * MOTOR_PWM_MAX) / MOTOR_PWM_MAX_SPEED);

  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, left_duty);
  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, right_duty);
}
