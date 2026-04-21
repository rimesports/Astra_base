#include "i2c_bus.h"
#include "astra_config.h"

I2C_HandleTypeDef hi2c1;

bool i2c_bus_init(void) {
  __HAL_RCC_I2C1_CLK_ENABLE();

  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10909CEC; // 100kHz on STM32L4 with 80MHz PCLK1
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

  if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
    return false;
  }
  return true;
}

bool i2c_write_register(uint8_t device_address, uint8_t reg, uint8_t value) {
  return HAL_I2C_Mem_Write(&hi2c1, device_address << 1, reg, I2C_MEMADD_SIZE_8BIT, &value, 1, 100) == HAL_OK;
}

bool i2c_write_registers(uint8_t device_address, uint8_t reg, const uint8_t *data, uint16_t length) {
  return HAL_I2C_Mem_Write(&hi2c1, device_address << 1, reg, I2C_MEMADD_SIZE_8BIT, (uint8_t *)data, length, 200) == HAL_OK;
}

bool i2c_read_registers(uint8_t device_address, uint8_t reg, uint8_t *buffer, uint16_t length) {
  return HAL_I2C_Mem_Read(&hi2c1, device_address << 1, reg, I2C_MEMADD_SIZE_8BIT, buffer, length, 200) == HAL_OK;
}

bool i2c_read_register_u16(uint8_t device_address, uint8_t reg, uint16_t *value) {
  uint8_t data[2] = {0};
  if (!i2c_read_registers(device_address, reg, data, 2)) {
    return false;
  }
  *value = (uint16_t)((data[0] << 8) | data[1]);
  return true;
}
