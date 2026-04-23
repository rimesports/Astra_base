#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  // Command targets (written by json_cmd, read by main loop)
  int16_t target_left;          // normalized -0.5..+0.5 scaled to -100..+100 (T:1/T:13)
  int16_t target_right;
  bool direct_pwm_mode;         // true when T:11 was last command
  int16_t pwm_left;             // raw PWM -255..+255 (T:11)
  int16_t pwm_right;
  bool command_received;
  uint32_t last_command_ms;

  // Motor/sensor state (written by main loop, read by telemetry)
  float rpm_left;
  float rpm_right;

  // IMU
  float roll;
  float pitch;
  float yaw;
  float imu_temp;               // °C from MPU-6050 temp register

  // Battery
  float battery_voltage;
  float battery_current;

  // Continuous feedback config (written by T:130/T:131, read by main loop)
  bool continuous_fb;           // true = send T:1001 periodically
  uint32_t fb_interval_ms;      // telemetry interval in ms (default: TELEMETRY_PERIOD_MS)

  // Optional PID tuning/debug telemetry
  bool pid_debug_enabled;
  float pid_profiled_left;
  float pid_profiled_right;
  float pid_output_left;
  float pid_output_right;
  float pid_integrator_left;
  float pid_integrator_right;
  bool pid_sat_high_left;
  bool pid_sat_high_right;
  bool pid_sat_low_left;
  bool pid_sat_low_right;
  bool pid_i_freeze_left;
  bool pid_i_freeze_right;
} RobotState;

extern RobotState g_state;

void shared_state_init(void);
void shared_state_feed_heartbeat(void);

#ifdef __cplusplus
}
#endif
