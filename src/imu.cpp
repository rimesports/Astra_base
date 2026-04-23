#include "imu.h"
#include "i2c_bus.h"
#include "astra_config.h"
#include "stm32f4xx_hal.h"
#include <math.h>
#include <string.h>

// ─── MPU-6050 register map ────────────────────────────────────────────────────
#define MPU_ADDR            0x68        // AD0 = GND
#define MPU_REG_SMPLRT_DIV  0x19
#define MPU_REG_CONFIG      0x1A
#define MPU_REG_GYRO_CFG    0x1B       // ±250 deg/s = 0x00
#define MPU_REG_ACCEL_CFG   0x1C       // ±2g       = 0x00
#define MPU_REG_ACCEL_XOUT  0x3B       // 6 bytes: AX_H AX_L AY_H AY_L AZ_H AZ_L
#define MPU_REG_TEMP_OUT    0x41       // 2 bytes
#define MPU_REG_GYRO_XOUT   0x43       // 6 bytes: GX_H GX_L GY_H GY_L GZ_H GZ_L
#define MPU_REG_PWR_MGMT_1  0x6B
#define MPU_REG_WHO_AM_I    0x75       // always returns 0x68

#define MPU_CHIP_ID         0x68

// Sensitivity at default full-scale range
#define ACCEL_LSB_PER_G     16384.0f   // ±2g
#define GYRO_LSB_PER_DPS    131.0f     // ±250 deg/s

// Complementary filter coefficient (0=accel only, 1=gyro only)
// 0.96 retains gyro for fast motion, corrects drift via accel
#define CF_ALPHA            0.96f

// ─── State ────────────────────────────────────────────────────────────────────
// Calibration offsets (raw counts) — zeroed at init, set by imu_calibrate()
static int32_t off_ax = 0, off_ay = 0, off_az = 0;
static int32_t off_gx = 0, off_gy = 0, off_gz = 0;

static float roll_deg  = 0.0f;
static float pitch_deg = 0.0f;
static float yaw_deg   = 0.0f;   // gyro-integrated only — drifts without mag
static float temp_c    = 0.0f;
static float ax_ms2    = 0.0f;
static float ay_ms2    = 0.0f;
static float az_ms2    = 0.0f;
static float gx_dps    = 0.0f;
static float gy_dps    = 0.0f;
static float gz_dps    = 0.0f;
static uint32_t last_update_ms = 0;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static bool mpu_write(uint8_t reg, uint8_t value) {
  return i2c_write_register(MPU_ADDR, reg, value);
}

static bool mpu_read(uint8_t reg, uint8_t *buf, uint8_t len) {
  return i2c_read_registers(MPU_ADDR, reg, buf, len);
}

static inline int16_t to_int16(uint8_t hi, uint8_t lo) {
  return (int16_t)((hi << 8) | lo);
}

// ─── Init ─────────────────────────────────────────────────────────────────────
bool imu_init(void) {
  // Reset chip, then wake from sleep
  if (!mpu_write(MPU_REG_PWR_MGMT_1, 0x80)) return false;  // device reset
  HAL_Delay(100);
  if (!mpu_write(MPU_REG_PWR_MGMT_1, 0x00)) return false;  // wake, internal osc
  HAL_Delay(10);

  // Verify chip ID
  uint8_t id = 0;
  if (!mpu_read(MPU_REG_WHO_AM_I, &id, 1)) return false;
  if (id != MPU_CHIP_ID) return false;

  // Sample rate: 1 kHz / (1 + 7) = 125 Hz
  if (!mpu_write(MPU_REG_SMPLRT_DIV, 0x07)) return false;
  // DLPF: 44 Hz accel / 42 Hz gyro bandwidth
  if (!mpu_write(MPU_REG_CONFIG, 0x03))     return false;
  // Gyro: ±250 deg/s
  if (!mpu_write(MPU_REG_GYRO_CFG, 0x00))  return false;
  // Accel: ±2g
  if (!mpu_write(MPU_REG_ACCEL_CFG, 0x00)) return false;

  last_update_ms = HAL_GetTick();
  return true;
}

// ─── Update — complementary filter ───────────────────────────────────────────
bool imu_update(void) {
  uint8_t buf[14] = {0};
  // Read accel (6) + temp (2) + gyro (6) in one burst from 0x3B
  if (!mpu_read(MPU_REG_ACCEL_XOUT, buf, 14)) return false;

  // Raw accel (with calibration offsets applied)
  int16_t raw_ax = to_int16(buf[0],  buf[1])  - (int16_t)off_ax;
  int16_t raw_ay = to_int16(buf[2],  buf[3])  - (int16_t)off_ay;
  int16_t raw_az = to_int16(buf[4],  buf[5])  - (int16_t)off_az;

  // Raw temp (no offset)
  int16_t raw_t  = to_int16(buf[6],  buf[7]);

  // Raw gyro (with calibration offsets applied)
  int16_t raw_gx = to_int16(buf[8],  buf[9])  - (int16_t)off_gx;
  int16_t raw_gy = to_int16(buf[10], buf[11]) - (int16_t)off_gy;
  int16_t raw_gz = to_int16(buf[12], buf[13]) - (int16_t)off_gz;

  // Convert
  float ax = raw_ax / ACCEL_LSB_PER_G;   // g
  float ay = raw_ay / ACCEL_LSB_PER_G;
  float az = raw_az / ACCEL_LSB_PER_G;
  gx_dps   = raw_gx / GYRO_LSB_PER_DPS;
  gy_dps   = raw_gy / GYRO_LSB_PER_DPS;
  gz_dps   = raw_gz / GYRO_LSB_PER_DPS;
  temp_c   = raw_t  / 340.0f + 36.53f;

  // m/s² (gravity-referenced, not compensated)
  ax_ms2 = ax * 9.81f;
  ay_ms2 = ay * 9.81f;
  az_ms2 = az * 9.81f;

  // dt
  uint32_t now = HAL_GetTick();
  float dt = (now - last_update_ms) * 0.001f;
  if (dt <= 0.0f || dt > 1.0f) dt = 0.01f;  // clamp on first call / stale
  last_update_ms = now;

  // Accel-derived angles (reliable when stationary)
  float accel_roll  = atan2f(ay, az) * (180.0f / 3.14159265f);
  float accel_pitch = atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0f / 3.14159265f);

  // Complementary filter
  roll_deg  = CF_ALPHA * (roll_deg  + gx_dps * dt) + (1.0f - CF_ALPHA) * accel_roll;
  pitch_deg = CF_ALPHA * (pitch_deg + gy_dps * dt) + (1.0f - CF_ALPHA) * accel_pitch;
  yaw_deg  += gz_dps * dt;   // gyro-only, drifts — no magnetometer on MPU-6050

  return true;
}

// ─── Calibration ─────────────────────────────────────────────────────────────
// Place robot flat and still. Collects `samples` readings, averages them.
// Gyro offset: average should be 0 → offset = average
// Accel offset: ax/ay should be 0, az should be +1g (16384) → offset = avg - expected
bool imu_calibrate(uint16_t samples) {
  int64_t sum_ax = 0, sum_ay = 0, sum_az = 0;
  int64_t sum_gx = 0, sum_gy = 0, sum_gz = 0;

  for (uint16_t i = 0; i < samples; i++) {
    uint8_t buf[14] = {0};
    if (!mpu_read(MPU_REG_ACCEL_XOUT, buf, 14)) return false;
    sum_ax += to_int16(buf[0],  buf[1]);
    sum_ay += to_int16(buf[2],  buf[3]);
    sum_az += to_int16(buf[4],  buf[5]);
    sum_gx += to_int16(buf[8],  buf[9]);
    sum_gy += to_int16(buf[10], buf[11]);
    sum_gz += to_int16(buf[12], buf[13]);
    HAL_Delay(2);  // ~500 Hz effective sample rate
  }

  off_ax = (int32_t)(sum_ax / samples);
  off_ay = (int32_t)(sum_ay / samples);
  off_az = (int32_t)(sum_az / samples) - 16384;  // preserve 1g on Z
  off_gx = (int32_t)(sum_gx / samples);
  off_gy = (int32_t)(sum_gy / samples);
  off_gz = (int32_t)(sum_gz / samples);

  // Reset filter state so stale angles don't pollute post-cal readings
  roll_deg = 0.0f;
  pitch_deg = 0.0f;
  yaw_deg = 0.0f;
  last_update_ms = HAL_GetTick();
  return true;
}

// ─── Getters ──────────────────────────────────────────────────────────────────
float imu_get_roll(void)  { return roll_deg; }
float imu_get_pitch(void) { return pitch_deg; }
float imu_get_yaw(void)   { return yaw_deg; }
float imu_get_temp(void)  { return temp_c; }
float imu_get_ax(void)    { return ax_ms2; }
float imu_get_ay(void)    { return ay_ms2; }
float imu_get_az(void)    { return az_ms2; }
float imu_get_gx(void)    { return gx_dps; }
float imu_get_gy(void)    { return gy_dps; }
float imu_get_gz(void)    { return gz_dps; }
