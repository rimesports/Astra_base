# IMU & Current Sensor Bring-Up Log
**Date:** 2026-04-20
**Board:** STM32 Nucleo-L476RG
**Hardware:** MPU-6050 (GY-521), INA219 current sensor

---

## Wiring Summary

### MPU-6050 (GY-521)
| GY-521 Pin | STM32 Pin | Nucleo Header |
|-----------|-----------|---------------|
| VCC | 3.3V | CN6 pin 4 |
| GND | GND | CN6 pin 6 |
| SCL | PB8 (I2C1_SCL) | D15 / CN8 pin 3 |
| SDA | PB9 (I2C1_SDA) | D14 / CN8 pin 4 |
| AD0 | GND | GND |

- AD0 = GND → I2C address **0x68**
- AD0 = 3.3V → I2C address 0x69 (alternate)

### INA219
| INA219 Pin | Connection |
|-----------|------------|
| VCC | 3.3V |
| GND | GND (shared with battery negative) |
| SCL | PB8 (shared I2C bus) |
| SDA | PB9 (shared I2C bus) |
| VIN+ | Battery positive |
| VIN- | MDD10A B+ (motor driver input) |

INA219 shunt sits in series on the positive battery rail feeding the MDD10A.
Both devices share the same I2C1 bus (PB8/PB9).

---

## IMU Bring-Up

### BNO055 — Failed (defective board)

Originally planned to use BNO055 (address 0x28). After extended debugging:
- I2C bus confirmed working (INA219 ACKed at 0x40)
- Full address sweep (T:128) found no BNO055 at any address
- Hardware reset via PB10 (D6) made no difference
- Board declared defective — likely counterfeit chip

**Lesson:** Avoid generic/unbranded BNO055 modules. High counterfeit rate.
Use Adafruit BNO055 (#2472) or substitute with MPU-6050.

### MPU-6050 — Success

Replaced BNO055 with GY-521 (MPU-6050). First scan confirmed immediately:
```
{"T":127,"mpu6050_68":{"ack":true,"id":"0x68"},"ina219_40":{"ack":true,"cfg":"0x3FFF"}}
```

First IMU query:
```
{"T":126}
→ {"T":1002,"r":-1.2,"p":1.8,"y":-0.0,"temp":23.6,"ax":-6.993,"ay":-3.620,"az":6.099,"gx":2.88,"gy":1.88,"gz":-0.14,"ok":true}
```

---

## Issues Found & Resolved

### Issue 1 — I2C clock not enabled
**Symptom:** T:127 returned `ack:false` for all devices. No activity on SCL/SDA.
**Root cause:** `__HAL_RCC_I2C1_CLK_ENABLE()` was never called. The I2C peripheral had no clock — GPIO was configured as AF4 but the peripheral itself was dead.
**Fix:** Added `__HAL_RCC_I2C1_CLK_ENABLE()` at the start of `i2c_bus_init()` in [i2c_bus.cpp](../src/i2c_bus.cpp).
**Confirmed by:** `err:4` (HAL_I2C_ERROR_AF) appeared after fix — bus was now driving but device not ACKing.

### Issue 2 — Internal pull-ups too weak
**Symptom:** BNO055 not ACKing even with I2C clock enabled.
**Root cause:** STM32 GPIO internal pull-ups are ~40kΩ — too weak for I2C. Rise time too slow for reliable communication.
**Fix:** Added INA219 board (with 10kΩ onboard pull-ups) to the bus. Effective pull-up became ~8kΩ (10kΩ ∥ 40kΩ), sufficient for 100kHz.
**Note:** For reliable long-term use, add dedicated 4.7kΩ pull-up resistors from SCL and SDA to 3.3V.

### Issue 3 — Wrong I2C timing register value
**Symptom:** Potential ACK failures at high speed.
**Root cause:** Original timing value `0x00707CBB` generated ~256kHz, not 100kHz.
**Fix:** Corrected to `0x10909CEC` (true 100kHz on STM32L4 with 80MHz PCLK1).

### Issue 4 — BNO055 defective
**Symptom:** Full I2C sweep (T:128) found no device at any address after all software fixes.
**Root cause:** Counterfeit/defective BNO055 chip on generic breakout board.
**Fix:** Replaced with MPU-6050 (GY-521). Rewrote `imu.cpp` with complementary filter.

---

## Software Changes

### imu.cpp — Full rewrite for MPU-6050
- Chip reset via PWR_MGMT_1, WHO_AM_I verification (expects 0x68)
- Single 14-byte burst read: accel (6) + temp (2) + gyro (6) from 0x3B
- Complementary filter (α=0.96) for roll and pitch
- Yaw: gyro-integrated only — drifts without magnetometer
- Sensitivity: ±2g (16384 LSB/g), ±250 deg/s (131 LSB/deg/s)

### astra_config.h
- Replaced `BNO055_I2C_ADDRESS 0x28` with `MPU6050_I2C_ADDRESS 0x68`
- Added `BNO055_RST_PIN / BNO055_RST_PORT` (PB10/GPIOB) — wired but unused now

### i2c_bus.cpp
- Added `__HAL_RCC_I2C1_CLK_ENABLE()` before `HAL_I2C_Init()`
- Corrected timing to `0x10909CEC` (100kHz)

### json_cmd.cpp
- T:127 now scans MPU-6050 at 0x68 (WHO_AM_I reg 0x75) and INA219 at 0x40
- T:128 full bus sweep retained for diagnostics

---

## IMU Limitations (MPU-6050 vs BNO055)

| Feature | MPU-6050 | BNO055 |
|---------|---------|--------|
| Onboard fusion | No — host computes | Yes |
| Roll / Pitch | Complementary filter ✓ | Direct ✓ |
| Yaw | Gyro-only (drifts) | Absolute (magnetometer) |
| Magnetometer | No | Yes |
| Cost | ~$2 | ~$35 |

For robot chassis telemetry (T:1001 / T:1002), roll and pitch are reliable. Yaw will drift over time — acceptable for short runs, not for long-term heading hold.

---

## Final Verified State

| Test | Result |
|------|--------|
| MPU-6050 detected at 0x68 | ✓ |
| WHO_AM_I returns 0x68 | ✓ |
| T:126 returns real roll/pitch/temp | ✓ |
| Temperature reads ~23°C (room temp) | ✓ |
| INA219 detected at 0x40 | ✓ (pending wire recheck) |

---

## Next Steps
- Recheck INA219 wiring (ack:false after MPU-6050 bring-up session)
- Wire INA219 VIN+ → battery positive, VIN- → MDD10A B+ for motor current measurement
- Complete T:150 motor current sweep to characterize motor draw at different PWM levels
- Order replacement BNO055 (Adafruit #2472) for future absolute yaw capability
