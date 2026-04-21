#include "pid.h"

void pid_init(PIDController *pid, float kp, float ki, float kd, float output_min, float output_max) {
  pid->kp = kp;
  pid->ki = ki;
  pid->kd = kd;
  pid->integrator = 0.0f;
  pid->prev_error = 0.0f;
  pid->output_min = output_min;
  pid->output_max = output_max;
  pid->integrator_min = output_min;
  pid->integrator_max = output_max;
}

float pid_compute(PIDController *pid, float setpoint, float measurement, float dt) {
  float error = setpoint - measurement;
  pid->integrator += error * dt;
  if (pid->integrator > pid->integrator_max) {
    pid->integrator = pid->integrator_max;
  } else if (pid->integrator < pid->integrator_min) {
    pid->integrator = pid->integrator_min;
  }

  float derivative = 0.0f;
  if (dt > 0.0f) {
    derivative = (error - pid->prev_error) / dt;
  }

  float output = pid->kp * error + pid->ki * pid->integrator + pid->kd * derivative;
  if (output > pid->output_max) {
    output = pid->output_max;
  } else if (output < pid->output_min) {
    output = pid->output_min;
  }

  pid->prev_error = error;
  return output;
}
