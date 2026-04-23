#include "pid.h"

void pid_init(PIDController *pid, float kp, float ki, float kd, float kf,
              float output_min, float output_max) {
  pid->kp = kp;
  pid->ki = ki;
  pid->kd = kd;
  pid->kf = kf;
  pid->integrator     = 0.0f;
  pid->prev_measurement = 0.0f;
  pid->output_min     = output_min;
  pid->output_max     = output_max;
  pid->integrator_min = output_min;
  pid->integrator_max = output_max;
}

float pid_compute(PIDController *pid, float setpoint, float measurement, float dt) {
  float error = setpoint - measurement;

  // ── Integrator with clamping (anti-windup) ─────────────────────────────────
  pid->integrator += error * dt;
  if (pid->integrator > pid->integrator_max) {
    pid->integrator = pid->integrator_max;
  } else if (pid->integrator < pid->integrator_min) {
    pid->integrator = pid->integrator_min;
  }

  // ── Derivative on measurement ──────────────────────────────────────────────
  // Using −Δmeasurement instead of Δerror removes the "derivative kick":
  // a step change in setpoint would spike Δerror but leaves Δmeasurement
  // smooth because the motor cannot accelerate instantaneously.
  // Sign: d(error)/dt = −d(measurement)/dt when setpoint is constant, so
  // negating gives the same direction as the conventional derivative term.
  float derivative = 0.0f;
  if (dt > 0.0f) {
    derivative = -(measurement - pid->prev_measurement) / dt;
  }
  pid->prev_measurement = measurement;

  // ── Output: feedforward + PID ──────────────────────────────────────────────
  // Feedforward (kf × setpoint) provides an open-loop estimate of the required
  // output.  The PID terms then only need to handle the residual error caused
  // by load disturbances, friction, and model mismatch.
  float output = pid->kf * setpoint
               + pid->kp * error
               + pid->ki * pid->integrator
               + pid->kd * derivative;

  if (output > pid->output_max) {
    output = pid->output_max;
  } else if (output < pid->output_min) {
    output = pid->output_min;
  }

  return output;
}

void pid_reset(PIDController *pid) {
  pid->integrator       = 0.0f;
  pid->prev_measurement = 0.0f;
}
