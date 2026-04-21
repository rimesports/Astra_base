#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern I2C_HandleTypeDef hi2c1;

bool i2c_bus_init(void);
bool i2c_write_register(uint8_t device_address, uint8_t reg, uint8_t value);
bool i2c_write_registers(uint8_t device_address, uint8_t reg, const uint8_t *data, uint16_t length);
bool i2c_read_registers(uint8_t device_address, uint8_t reg, uint8_t *buffer, uint16_t length);
bool i2c_read_register_u16(uint8_t device_address, uint8_t reg, uint16_t *value);

#ifdef __cplusplus
}
#endif
