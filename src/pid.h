#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── PIDController ────────────────────────────────────────────────────────────
//
//  Three improvements over a textbook PID:
//
//  1. Feedforward (kf):
//     output += kf × setpoint before the feedback terms.
//     Handles the steady-state motor gain open-loop so the integrator only
//     needs to cancel disturbances, not the bulk of the load.  Set kf first
//     with Ki=0: raise until actual RPM ≈ setpoint, then re-enable Ki.
//
//  2. Derivative on measurement (not error):
//     derivative = −Δmeasurement / dt   (not Δerror / dt)
//     A step change in setpoint does not cause Δerror spikes because the
//     setpoint term cancels when taking the difference of two error samples.
//     Using measurement directly avoids that "derivative kick".
//
//  3. Integrator clamping (anti-windup):
//     Integrator is clipped to [output_min, output_max] each cycle.
//     With feedforward carrying the steady-state load the integrator stays
//     small and rarely hits the clamp, which reduces wind-up during saturation.

typedef struct {
  float kp;
  float ki;
  float kd;
  float kf;               // feedforward gain (see note above)
  float integrator;
  float prev_measurement; // last measurement — used by derivative-on-measurement
  float output_min;
  float output_max;
  float integrator_min;
  float integrator_max;
} PIDController;

// kf = 0.0f disables feedforward (pure PID behaviour).
void pid_init(PIDController *pid, float kp, float ki, float kd, float kf,
              float output_min, float output_max);
float pid_compute(PIDController *pid, float setpoint, float measurement, float dt);
void pid_reset(PIDController *pid);  // zero integrator and prev_measurement (call on mode switch / estop)

#ifdef __cplusplus
}
#endif
