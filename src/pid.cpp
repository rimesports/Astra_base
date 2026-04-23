#include "pid.h"

void pid_init(PIDController *pid, float kp, float ki, float kd, float kf,
              float output_min, float output_max) {
  pid->kp = kp;
  pid->ki = ki;
  pid->kd = kd;
  pid->kf = kf;
  pid->integrator = 0.0f;
  pid->prev_measurement = 0.0f;
  pid->has_prev_measurement = 0;
  pid->last_saturated_high = 0;
  pid->last_saturated_low = 0;
  pid->last_integrator_frozen = 0;
  pid->last_unsat_output = 0.0f;
  pid->last_output = 0.0f;
  pid->output_min = output_min;
  pid->output_max = output_max;
  pid->integrator_min = output_min;
  pid->integrator_max = output_max;
}

float pid_compute(PIDController *pid, float setpoint, float measurement, float dt) {
  float error = setpoint - measurement;

  // Derivative on measurement avoids derivative kick on setpoint steps.
  float derivative = 0.0f;
  if (pid->has_prev_measurement && dt > 0.0f) {
    derivative = -(measurement - pid->prev_measurement) / dt;
  }
  pid->prev_measurement = measurement;
  pid->has_prev_measurement = 1;

  // Compute the provisional output without changing the integrator first.
  float base_output = pid->kf * setpoint
                    + pid->kp * error
                    + pid->kd * derivative;
  float unsat_output = base_output + pid->ki * pid->integrator;

  // Saturation-aware anti-windup:
  // if output is already above the high rail and the current error would
  // push it even higher, freeze the integrator. Mirror the same logic for
  // the low rail.
  bool saturating_high = (unsat_output > pid->output_max);
  bool saturating_low = (unsat_output < pid->output_min);
  bool drive_further_high = saturating_high && (error > 0.0f);
  bool drive_further_low = saturating_low && (error < 0.0f);

  pid->last_saturated_high = saturating_high ? 1u : 0u;
  pid->last_saturated_low = saturating_low ? 1u : 0u;
  pid->last_integrator_frozen = (drive_further_high || drive_further_low) ? 1u : 0u;
  pid->last_unsat_output = unsat_output;

  if (!(drive_further_high || drive_further_low)) {
    pid->integrator += error * dt;
    if (pid->integrator > pid->integrator_max) {
      pid->integrator = pid->integrator_max;
    } else if (pid->integrator < pid->integrator_min) {
      pid->integrator = pid->integrator_min;
    }
  }

  float output = pid->kf * setpoint
               + pid->kp * error
               + pid->ki * pid->integrator
               + pid->kd * derivative;

  if (output > pid->output_max) {
    output = pid->output_max;
  } else if (output < pid->output_min) {
    output = pid->output_min;
  }

  pid->last_output = output;

  return output;
}

void pid_reset(PIDController *pid) {
  pid->integrator = 0.0f;
  pid->prev_measurement = 0.0f;
  pid->has_prev_measurement = 0;
  pid->last_saturated_high = 0;
  pid->last_saturated_low = 0;
  pid->last_integrator_frozen = 0;
  pid->last_unsat_output = 0.0f;
  pid->last_output = 0.0f;
}
