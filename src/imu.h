#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool imu_init(void);
bool imu_update(void);
bool imu_calibrate(uint16_t samples);  // call while robot is flat and still
float imu_get_roll(void);
float imu_get_pitch(void);
float imu_get_yaw(void);
float imu_get_temp(void);
// Linear acceleration (m/s², gravity-referenced) and gyro (deg/s)
// from MPU-6050 complementary filter — used in T:1002 IMU snapshot
float imu_get_ax(void);
float imu_get_ay(void);
float imu_get_az(void);
float imu_get_gx(void);
float imu_get_gy(void);
float imu_get_gz(void);

#ifdef __cplusplus
}
#endif
