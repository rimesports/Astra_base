#include "ina219.h"
#include "i2c_bus.h"
#include "astra_config.h"

#define INA219_REG_CONFIG       0x00
#define INA219_REG_SHUNTVOLTAGE 0x01
#define INA219_REG_BUSVOLTAGE   0x02
#define INA219_REG_CURRENT      0x04
#define INA219_REG_CALIBRATION  0x05   // writable — sets current/power LSB
#define INA219_CONFIG_RESET     0x8000

// 0.1 Ω shunt, 100 µA/LSB.
// Formula: Cal = trunc(0.04096 / (Current_LSB × R_shunt))
//               = trunc(0.04096 / (100e-6 × 0.1)) = 4096
static const uint16_t ina219_calibration = 4096;

static bool write16(uint8_t reg, uint16_t value) {
  uint8_t data[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
  return i2c_write_registers(INA219_I2C_ADDRESS, reg, data, 2);
}

static bool read16(uint8_t reg, int16_t *value) {
  uint8_t data[2] = {0};
  if (!i2c_read_registers(INA219_I2C_ADDRESS, reg, data, 2)) {
    return false;
  }
  *value = (int16_t)((data[0] << 8) | data[1]);
  return true;
}

bool ina219_init(void) {
  // Reset to defaults, then re-configure.
  if (!write16(INA219_REG_CONFIG, INA219_CONFIG_RESET)) {
    return false;
  }

  // 32 V bus range, PGA /8 (±320 mV shunt), 12-bit ADC, continuous.
  // 0x3FFF = BRNG=1(32V) PG=11(/8) BADC=1111(128avg) SADC=1111(128avg) MODE=111
  if (!write16(INA219_REG_CONFIG, 0x3FFF)) {
    return false;
  }

  // Write calibration register (0x05) — not the current readback (0x04).
  if (!write16(INA219_REG_CALIBRATION, ina219_calibration)) {
    return false;
  }

  return true;
}

bool ina219_read(float *voltage, float *current) {
  int16_t bus_raw = 0;
  if (!read16(INA219_REG_BUSVOLTAGE, &bus_raw)) {
    return false;
  }

  uint16_t bus = (uint16_t)bus_raw >> 3;
  *voltage = bus * 4e-3f;

  int16_t current_raw = 0;
  if (!read16(INA219_REG_CURRENT, &current_raw)) {
    *current = 0.0f;
    return true;
  }

  *current = current_raw * 0.1f;
  return true;
}
