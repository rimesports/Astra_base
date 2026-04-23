#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t magic;
    uint32_t version;
    float kp;
    float ki;
    float kd;
    float kf;
    float imu_ax_off;
    float imu_ay_off;
    float imu_az_off;
    float imu_gx_off;
    float imu_gy_off;
    float imu_gz_off;
    uint32_t checksum;  // sum of all preceding words
} ConfigData;

bool config_store_save(const ConfigData *cfg);
bool config_store_load(ConfigData *cfg);
void config_store_defaults(ConfigData *cfg);

#ifdef __cplusplus
}
#endif
