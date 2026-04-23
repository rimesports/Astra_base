#include "shared_state.h"
#include "astra_config.h"
#include "stm32f4xx_hal.h"

RobotState g_state;

void shared_state_init(void) {
  // Detect reset cause before clearing flags (must happen before CLEAR_RESET_FLAGS)
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  g_state.fault_flags = 0;
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
    g_state.fault_flags |= FAULT_IWDG_RESET;
  }
  __HAL_RCC_CLEAR_RESET_FLAGS();

  // Check RTC BKP magic — set by hardfault_capture, survives watchdog reset
  if (RTC->BKP0R == HF_BKP_MAGIC_VAL) {
    g_state.fault_flags |= FAULT_HARDFAULT;
  }

  g_state.sys_state = SYS_IDLE;
  g_state.heartbeat_fault_notify = false;
  g_state.pid_kp = PID_KP;
  g_state.pid_ki = PID_KI;
  g_state.pid_kd = PID_KD;
  g_state.pid_kf = PID_KF;
  g_state.pid_gains_pending = false;

  g_state.target_left = 0;
  g_state.target_right = 0;
  g_state.direct_pwm_mode = false;
  g_state.pwm_left = 0;
  g_state.pwm_right = 0;
  g_state.command_received = false;
  g_state.last_command_ms = HAL_GetTick();
  g_state.rpm_left = 0.0f;
  g_state.rpm_right = 0.0f;
  g_state.tick_total_left  = 0;
  g_state.tick_total_right = 0;
  g_state.roll = 0.0f;
  g_state.pitch = 0.0f;
  g_state.yaw = 0.0f;
  g_state.imu_temp = 0.0f;
  g_state.battery_voltage = 0.0f;
  g_state.battery_current = 0.0f;
  // Default off — enable with {"T":130,"cmd":1}
  g_state.continuous_fb = false;
  g_state.fb_interval_ms = TELEMETRY_PERIOD_MS;
  g_state.pid_debug_enabled = false;
  g_state.pid_profiled_left = 0.0f;
  g_state.pid_profiled_right = 0.0f;
  g_state.pid_output_left = 0.0f;
  g_state.pid_output_right = 0.0f;
  g_state.pid_integrator_left = 0.0f;
  g_state.pid_integrator_right = 0.0f;
  g_state.pid_sat_high_left = false;
  g_state.pid_sat_high_right = false;
  g_state.pid_sat_low_left = false;
  g_state.pid_sat_low_right = false;
  g_state.pid_i_freeze_left = false;
  g_state.pid_i_freeze_right = false;
}

void shared_state_feed_heartbeat(void) {
  g_state.command_received = true;
  g_state.last_command_ms = HAL_GetTick();
  if (g_state.sys_state == SYS_IDLE) {
    g_state.sys_state = SYS_RUNNING;
  }
}
