#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// PIDController
//
// Three improvements over a textbook PID:
//
// 1. Feedforward (kf):
//    output += kf * setpoint before the feedback terms.
//    Handles the steady-state motor gain open-loop so the integrator only
//    needs to cancel disturbances, not the bulk of the load. Set kf first
//    with Ki=0: raise until actual RPM ~= setpoint, then re-enable Ki.
//
// 2. Derivative on measurement (not error):
//    derivative = -d(measurement)/dt   (not d(error)/dt)
//    A step change in setpoint does not cause an error spike in the derivative
//    term, avoiding derivative kick.
//
// 3. Saturation-aware anti-windup:
//    The integrator is only allowed to grow when the actuator is not saturated,
//    or when the current error would drive the saturated output back toward the
//    usable range. This avoids winding up the integrator against the rail.
//    The integrator is still clamped to [integrator_min, integrator_max] as a
//    hard safety bound.

typedef struct {
  float kp;
  float ki;
  float kd;
  float kf;
  float integrator;
  float prev_measurement;
  uint8_t has_prev_measurement;
  uint8_t last_saturated_high;
  uint8_t last_saturated_low;
  uint8_t last_integrator_frozen;
  float last_unsat_output;
  float last_output;
  float output_min;
  float output_max;
  float integrator_min;
  float integrator_max;
} PIDController;

void pid_init(PIDController *pid, float kp, float ki, float kd, float kf,
              float output_min, float output_max);
float pid_compute(PIDController *pid, float setpoint, float measurement, float dt);
void pid_reset(PIDController *pid);

#ifdef __cplusplus
}
#endif
