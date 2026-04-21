#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float kp;
  float ki;
  float kd;
  float integrator;
  float prev_error;
  float output_min;
  float output_max;
  float integrator_min;
  float integrator_max;
} PIDController;

void pid_init(PIDController *pid, float kp, float ki, float kd, float output_min, float output_max);
float pid_compute(PIDController *pid, float setpoint, float measurement, float dt);

#ifdef __cplusplus
}
#endif
