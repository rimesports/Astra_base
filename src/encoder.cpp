#include "encoder.h"
#include "astra_config.h"
#include "stm32f4xx_hal.h"

static volatile int32_t left_count = 0;
static volatile int32_t right_count = 0;
static uint32_t last_update_ms = 0;
static float left_rpm = 0.0f;
static float right_rpm = 0.0f;

// Previous quadrature state (bits: A=bit1, B=bit0)
static volatile uint8_t left_prev_state  = 0;
static volatile uint8_t right_prev_state = 0;

// 4-edge quadrature lookup table.
// Index = (prev_state << 2) | cur_state  (4-bit, 0-15)
// +1 = forward, -1 = backward, 0 = no change or invalid transition
static const int8_t quad_table[16] = {
   0, -1, +1,  0,   // prev=00: →00,→01,→10,→11
  +1,  0,  0, -1,   // prev=01: →00,→01,→10,→11
  -1,  0,  0, +1,   // prev=10: →00,→01,→10,→11
   0, +1, -1,  0,   // prev=11: →00,→01,→10,→11
};

static void update_quadrature(volatile int32_t *count,
                              GPIO_TypeDef *a_port, uint16_t a_pin,
                              GPIO_TypeDef *b_port, uint16_t b_pin,
                              volatile uint8_t *prev_state) {
  uint8_t a   = (HAL_GPIO_ReadPin(a_port, a_pin) == GPIO_PIN_SET) ? 2u : 0u;
  uint8_t b   = (HAL_GPIO_ReadPin(b_port, b_pin) == GPIO_PIN_SET) ? 1u : 0u;
  uint8_t cur = a | b;
  *count += quad_table[(*prev_state << 2) | cur];
  *prev_state = cur;
}

void encoder_init(void) {
  last_update_ms = HAL_GetTick();
}

void encoder_exti_callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == ENCODER_LEFT_A_PIN || GPIO_Pin == ENCODER_LEFT_B_PIN) {
    update_quadrature(&left_count, ENCODER_LEFT_A_PORT, ENCODER_LEFT_A_PIN,
                      ENCODER_LEFT_B_PORT, ENCODER_LEFT_B_PIN, &left_prev_state);
  } else if (GPIO_Pin == ENCODER_RIGHT_A_PIN || GPIO_Pin == ENCODER_RIGHT_B_PIN) {
    update_quadrature(&right_count, ENCODER_RIGHT_A_PORT, ENCODER_RIGHT_A_PIN,
                      ENCODER_RIGHT_B_PORT, ENCODER_RIGHT_B_PIN, &right_prev_state);
  }
}

void encoder_update(void) {
  uint32_t now = HAL_GetTick();
  uint32_t delta_ms = now - last_update_ms;
  if (delta_ms == 0) {
    return;
  }

  int32_t left_snapshot = left_count;
  int32_t right_snapshot = right_count;
  left_count = 0;
  right_count = 0;

  float left_revs = (float)left_snapshot / (float)ENCODER_COUNTS_PER_REV;
  float right_revs = (float)right_snapshot / (float)ENCODER_COUNTS_PER_REV;
  float minutes = (float)delta_ms / 60000.0f;

  if (minutes > 0.0f) {
    left_rpm = left_revs / minutes;
    right_rpm = right_revs / minutes;
  }

  last_update_ms = now;
}

float encoder_get_left_rpm(void) {
  return left_rpm;
}

float encoder_get_right_rpm(void) {
  return right_rpm;
}
