#include "json_cmd.h"
#include "astra_config.h"
#include "serial_cmd.h"
#include "shared_state.h"
#include "config_store.h"
#include "main.h"
#include "imu.h"
#include "ina219.h"
#include "encoder.h"
#include "motor_ctrl.h"
#include "i2c_bus.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// HardFault capture registers from stm32f4xx_it.cpp
extern volatile uint32_t g_hf_pc, g_hf_lr, g_hf_xpsr;
extern volatile uint32_t g_hf_cfsr, g_hf_hfsr, g_hf_bfar, g_hf_mmfar;

static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ─── Minimal JSON helpers (no heap allocation) ────────────────────────────────

static bool parse_json_float(const char *text, const char *key, float *value) {
  char token[24];
  snprintf(token, sizeof(token), "\"%s\"", key);
  const char *pos = strstr(text, token);
  if (!pos) return false;
  pos = strchr(pos + strlen(token), ':');
  if (!pos) return false;
  *value = strtof(pos + 1, NULL);
  return true;
}

static bool parse_json_int(const char *text, const char *key, int *value) {
  char token[24];
  snprintf(token, sizeof(token), "\"%s\"", key);
  const char *pos = strstr(text, token);
  if (!pos) return false;
  pos = strchr(pos + strlen(token), ':');
  if (!pos) return false;
  *value = (int)strtol(pos + 1, NULL, 10);
  return true;
}

static bool parse_json_bool(const char *text, const char *key, bool *value) {
  char token[24];
  snprintf(token, sizeof(token), "\"%s\"", key);
  const char *pos = strstr(text, token);
  if (!pos) return false;
  pos = strchr(pos + strlen(token), ':');
  if (!pos) return false;
  pos++;
  while (*pos == ' ' || *pos == '\t') pos++;
  if (strncmp(pos, "true", 4) == 0)  { *value = true;  return true; }
  if (strncmp(pos, "false", 5) == 0) { *value = false; return true; }
  return false;
}

// ─── Command dispatcher ───────────────────────────────────────────────────────

void json_cmd_process_line(const char *line) {
  int tcode = 0;
  if (!parse_json_int(line, "T", &tcode)) return;

  switch (tcode) {

  // ── T:0  Stop motors immediately ─────────────────────────────────────────
  // {"T":0}
  // Matches Waveshare cleanup convention and gives Jetson shutdown a safe,
  // protocol-level stop command.
  case CMD_STOP:
    g_state.target_left = 0;
    g_state.target_right = 0;
    g_state.pwm_left = 0;
    g_state.pwm_right = 0;
    g_state.direct_pwm_mode = false;
    g_state.command_received = false;
    g_state.sys_state = SYS_IDLE;
    motor_ctrl_set_speed(0, 0);
    serial_send_line("{\"T\":0,\"ack\":true}");
    break;

  // ── T:1  Normalized speed control ─────────────────────────────────────────
  // {"T":1,"L":-0.5..0.5,"R":-0.5..0.5}
  // Matches Wave Rover / ugv_rpi base_ctrl.py convention.
  // Scale factor ×200 maps ±0.5 → ±100 (full motor range).
  case CMD_SPEED_CTRL: {
    float left = 0.0f;
    float right = 0.0f;
    if (parse_json_float(line, "L", &left) && parse_json_float(line, "R", &right)) {
      g_state.target_left  = (int16_t)CLAMP(left  * 200.0f, -100.0f, 100.0f);
      g_state.target_right = (int16_t)CLAMP(right * 200.0f, -100.0f, 100.0f);
      g_state.direct_pwm_mode = false;
      shared_state_feed_heartbeat();
    }
    break;
  }

  // ── T:11  Direct PWM — bypasses speed scaling ─────────────────────────────
  // {"T":11,"L":-255..255,"R":-255..255}
  // Stored raw; main loop scales -255..255 → -100..100 when applying.
  case CMD_PWM_INPUT: {
    int left = 0;
    int right = 0;
    if (parse_json_int(line, "L", &left) && parse_json_int(line, "R", &right)) {
      g_state.pwm_left  = (int16_t)CLAMP(left,  -255, 255);
      g_state.pwm_right = (int16_t)CLAMP(right, -255, 255);
      g_state.direct_pwm_mode = true;
      shared_state_feed_heartbeat();
      char ack[64];
      snprintf(ack, sizeof(ack), "{\"T\":11,\"ack\":1,\"L\":%d,\"R\":%d}",
               (int)((int32_t)g_state.pwm_left  * 100 / 255),
               (int)((int32_t)g_state.pwm_right * 100 / 255));
      serial_send_line(ack);
    }
    break;
  }

  // ── T:13  ROS differential drive ──────────────────────────────────────────
  // {"T":13,"linear":m/s,"angular":rad/s}
  // Conversion matches ESP32: 1.0 m/s → 0.5 normalized → 100% speed.
  case CMD_ROS_CTRL: {
    float linear = 0.0f;
    float angular = 0.0f;
    if (parse_json_float(line, "linear", &linear) &&
        parse_json_float(line, "angular", &angular)) {
      float v  = linear  * 0.5f;
      float w  = angular * 0.25f;
      float fl = CLAMP(v - w, -0.5f, 0.5f);
      float fr = CLAMP(v + w, -0.5f, 0.5f);
      g_state.target_left  = (int16_t)CLAMP(fl * 200.0f, -100.0f, 100.0f);
      g_state.target_right = (int16_t)CLAMP(fr * 200.0f, -100.0f, 100.0f);
      g_state.direct_pwm_mode = false;
      shared_state_feed_heartbeat();
    }
    break;
  }

  // ── T:127  I2C bus scan — MPU-6050 (0x68) and INA219 (0x40) ────────────
  case 127: {
    if (hi2c1.State != HAL_I2C_STATE_READY) {
      HAL_I2C_DeInit(&hi2c1);
      HAL_I2C_Init(&hi2c1);
    }
    uint8_t mpu_id = 0;
    uint8_t ina_buf[2] = {0};
    bool ok68  = i2c_read_registers(0x68, 0x75, &mpu_id, 1);  // WHO_AM_I
    bool ok40  = i2c_read_registers(0x40, 0x00, ina_buf, 2);
    uint16_t ina_cfg = (uint16_t)((ina_buf[0] << 8) | ina_buf[1]);
    char buf[192];
    snprintf(buf, sizeof(buf),
      "{\"T\":127"
      ",\"mpu6050_68\":{\"ack\":%s,\"id\":\"0x%02X\"}"
      ",\"ina219_40\":{\"ack\":%s,\"cfg\":\"0x%04X\"}"
      "}",
      ok68 ? "true" : "false", mpu_id,
      ok40 ? "true" : "false", ina_cfg);
    serial_send_line(buf);
    break;
  }

  // ── T:150  Motor current sweep — both motors, 6 PWM steps ────────────────
  // {"T":150}  runs both motors from 0→25→50→75→100→0, reads INA219 at each
  case 150: {
    static const int16_t steps[] = {0, 25, 50, 75, 100};
    char buf[320];
    int pos = snprintf(buf, sizeof(buf), "{\"T\":150,\"steps\":[");
    for (int i = 0; i < 5; i++) {
      g_state.target_left      = steps[i];
      g_state.target_right     = steps[i];
      g_state.direct_pwm_mode  = false;
      shared_state_feed_heartbeat();
      vTaskDelay(pdMS_TO_TICKS(500));   // settle time
      float v = 0.0f, c = 0.0f;
      ina219_read(&v, &c);
      int n = snprintf(buf + pos, sizeof(buf) - pos,
                       "%s{\"pwm\":%d,\"v\":%.2f,\"i\":%.0f}",
                       i == 0 ? "" : ",", (int)steps[i], v, c);
      pos += n;
    }
    g_state.target_left  = 0;
    g_state.target_right = 0;
    snprintf(buf + pos, sizeof(buf) - pos, "]}");
    serial_send_line(buf);
    break;
  }

  // ── T:128  Full I2C bus sweep 0x01–0x7F ──────────────────────────────────
  case 128: {
    char buf[256];
    int pos = snprintf(buf, sizeof(buf), "{\"T\":128,\"found\":[");
    bool first = true;
    for (uint8_t addr = 0x01; addr < 0x78; addr++) {
      uint8_t dummy = 0;
      if (i2c_read_registers(addr, 0x00, &dummy, 1)) {
        int n = snprintf(buf + pos, sizeof(buf) - pos,
                         "%s\"0x%02X\"", first ? "" : ",", addr);
        pos += n;
        first = false;
      }
    }
    snprintf(buf + pos, sizeof(buf) - pos, "]}");
    serial_send_line(buf);
    break;
  }

  // ── T:160  IMU calibration — robot must be flat and stationary ───────────
  // {"T":160}  collects 500 samples (~1 second), reports offsets applied
  case 160: {
    serial_send_line("{\"T\":160,\"status\":\"calibrating\"}");
    bool ok = imu_calibrate(500);
    // Read one sample to show post-calibration values
    imu_update();
    char buf[128];
    snprintf(buf, sizeof(buf),
      "{\"T\":160,\"ok\":%s,\"r\":%.2f,\"p\":%.2f,\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f}",
      ok ? "true" : "false",
      imu_get_roll(), imu_get_pitch(),
      imu_get_gx(), imu_get_gy(), imu_get_gz());
    serial_send_line(buf);
    break;
  }

  // ── T:126  IMU query → synchronous T:1002 response ────────────────────────
  // control_task calls imu_update() at 50 Hz; data is at most CONTROL_PERIOD_MS old.
  case CMD_IMU_QUERY: {
    char buf[256];
    int len = snprintf(buf, sizeof(buf),
      "{\"T\":%d,\"r\":%.1f,\"p\":%.1f,\"y\":%.1f"
      ",\"temp\":%.1f"
      ",\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f"
      ",\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f"
      ",\"ok\":true}",
      FEEDBACK_IMU_DATA,
      imu_get_roll(), imu_get_pitch(), imu_get_yaw(),
      imu_get_temp(),
      imu_get_ax(), imu_get_ay(), imu_get_az(),
      imu_get_gx(), imu_get_gy(), imu_get_gz());
    if (len > 0) serial_send_line(buf);
    break;
  }

  // ── T:130  Feedback flow toggle ────────────────────────────────────────────
  // {"T":130,"cmd":1}  enable   {"T":130,"cmd":0}  disable
  case CMD_FEEDBACK_FLOW: {
    int cmd = 0;
    if (parse_json_int(line, "cmd", &cmd)) {
      g_state.continuous_fb = (cmd == 1);
    }
    break;
  }

  // ── T:131  Feedback interval ───────────────────────────────────────────────
  // {"T":131,"cmd":1,"interval":100}
  case CMD_FEEDBACK_INTERVAL: {
    int cmd = 0;
    int interval = 0;
    if (parse_json_int(line, "cmd", &cmd)) {
      g_state.continuous_fb = (cmd == 1);
    }
    if (parse_json_int(line, "interval", &interval) && interval > 0) {
      g_state.fb_interval_ms = (uint32_t)interval;
    }
    break;
  }

  // ── T:132  PID debug telemetry toggle ─────────────────────────────────────
  // {"T":132,"cmd":1}  enable   {"T":132,"cmd":0}  disable
  // When enabled, T:1001 feedback gains extra fields — see pid_tuning_guide.md.
  case CMD_PID_DEBUG: {
    int cmd = 0;
    if (parse_json_int(line, "cmd", &cmd)) {
      g_state.pid_debug_enabled = (cmd == 1);
      char buf[64];
      snprintf(buf, sizeof(buf), "{\"T\":132,\"ack\":true,\"cmd\":%d}", cmd == 1 ? 1 : 0);
      serial_send_line(buf);
    }
    break;
  }

  // ── T:200  System self-diagnostic ────────────────────────────────────────
  // {"T":200}  → one-shot health snapshot, no side-effects
  case CMD_SYSDIAG: {
    // Uptime and task count (always available)
    uint32_t tick   = HAL_GetTick();
    uint32_t ntasks = (uint32_t)uxTaskGetNumberOfTasks();

    // I2C: MPU-6050 WHO_AM_I
    uint8_t who = 0;
    bool imu_ack = i2c_read_registers(MPU6050_I2C_ADDRESS, 0x75, &who, 1);
    bool imu_id_ok = imu_ack && (who == MPU6050_I2C_ADDRESS);

    // I2C: INA219 config register ACK
    uint8_t ina_buf[2] = {0};
    bool ina_ack = i2c_read_registers(INA219_I2C_ADDRESS, 0x00, ina_buf, 2);

    // IMU data freshness — control_task calls imu_update() at 50 Hz; read cached values.
    float temp = imu_get_temp();
    bool imu_data_ok = (temp > -10.0f && temp < 85.0f);   // plausible silicon range
    bool temp_ok = imu_data_ok;

    // INA219 bus voltage — 0.00 means no load-side rail connected
    float batt_v = 0.0f, batt_i = 0.0f;
    ina219_read(&batt_v, &batt_i);
    bool batt_ok = (batt_v > 5.0f);   // >5 V = something is connected

    // GPIO readback — direction pins are output, IDR reflects actual pin state
    bool dir1 = (HAL_GPIO_ReadPin(MOTOR_LEFT_DIR_PORT,  MOTOR_LEFT_DIR_PIN)  == GPIO_PIN_SET);
    bool dir2 = (HAL_GPIO_ReadPin(MOTOR_RIGHT_DIR_PORT, MOTOR_RIGHT_DIR_PIN) == GPIO_PIN_SET);

    // TIM2 — must be running for PWM to work
    bool tim2_ok = ((TIM2->CR1 & TIM_CR1_CEN) != 0);

    // Stack watermarks (words remaining at high-water mark — higher = more headroom)
    UBaseType_t stk_ctrl  = h_control   ? uxTaskGetStackHighWaterMark(h_control)   : 0;
    UBaseType_t stk_ser   = h_serial    ? uxTaskGetStackHighWaterMark(h_serial)    : 0;
    UBaseType_t stk_telem = h_telemetry ? uxTaskGetStackHighWaterMark(h_telemetry) : 0;

    uint8_t usb_cfg = serial_usb_configured();
    uint8_t usb_open = serial_usb_port_open();
    uint32_t usb_tx_drop = serial_usb_tx_dropped();
    uint32_t usb_rx_drop = serial_usb_rx_dropped();
    uint32_t usb_tx_q = serial_usb_tx_queued();
    uint32_t usb_rx_q = serial_usb_rx_queued();

    char buf[512];
    snprintf(buf, sizeof(buf),
      "{\"T\":200"
      ",\"tick\":%lu,\"tasks\":%lu"
      ",\"i2c_imu\":%s,\"imu_id\":\"0x%02X\",\"imu_id_ok\":%s"
      ",\"imu_data\":%s,\"temp\":%.1f,\"temp_ok\":%s"
      ",\"i2c_ina\":%s,\"batt_v\":%.2f,\"batt_ok\":%s"
      ",\"dir1\":%d,\"dir2\":%d"
      ",\"tim2\":%s"
      ",\"usb_cfg\":%s,\"usb_open\":%s"
      ",\"usb_tx_q\":%lu,\"usb_rx_q\":%lu"
      ",\"usb_tx_drop\":%lu,\"usb_rx_drop\":%lu"
      ",\"stk_ctrl\":%lu,\"stk_ser\":%lu,\"stk_telem\":%lu"
      "}",
      tick, ntasks,
      imu_ack    ? "true" : "false", who, imu_id_ok  ? "true" : "false",
      imu_data_ok? "true" : "false", temp, temp_ok   ? "true" : "false",
      ina_ack    ? "true" : "false", batt_v, batt_ok ? "true" : "false",
      dir1 ? 1 : 0, dir2 ? 1 : 0,
      tim2_ok    ? "true" : "false",
      usb_cfg    ? "true" : "false", usb_open ? "true" : "false",
      (unsigned long)usb_tx_q, (unsigned long)usb_rx_q,
      (unsigned long)usb_tx_drop, (unsigned long)usb_rx_drop,
      (unsigned long)stk_ctrl, (unsigned long)stk_ser, (unsigned long)stk_telem);
    serial_send_line(buf);
    break;
  }

  // ── T:143  Echo — host uses this to verify connectivity ───────────────────
  case CMD_SERIAL_ECHO:
    serial_send_line(line);
    break;

  // ── T:201  HardFault register dump ────────────────────────────────────────
  // Reads RTC backup registers written by hardfault_capture().
  // valid=true means a hardfault occurred in a previous run.
  case CMD_HF_DUMP: {
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    uint32_t magic   = RTC->BKP0R;
    uint32_t hf_pc   = RTC->BKP1R;
    uint32_t hf_lr   = RTC->BKP2R;
    uint32_t hf_cfsr = RTC->BKP3R;
    uint32_t hf_hfsr = RTC->BKP4R;
    uint32_t hf_cnt  = RTC->BKP5R;
    uint32_t hf_type = RTC->BKP6R;
    static const char *type_str[] = {"hardfault", "stack_overflow", "malloc_failed"};
    const char *ts = (hf_type < 3) ? type_str[hf_type] : "unknown";
    char buf[256];
    snprintf(buf, sizeof(buf),
      "{\"T\":201,\"valid\":%s,\"type\":\"%s\""
      ",\"pc\":\"0x%08lX\",\"lr\":\"0x%08lX\""
      ",\"cfsr\":\"0x%08lX\",\"hfsr\":\"0x%08lX\""
      ",\"cnt\":%lu,\"faults\":\"0x%lX\"}",
      magic == HF_BKP_MAGIC_VAL ? "true" : "false", ts,
      hf_pc, hf_lr, hf_cfsr, hf_hfsr,
      hf_cnt, g_state.fault_flags);
    serial_send_line(buf);
    break;
  }

  // ── T:210  Clear fault flags and fault BKP magic ──────────────────────────
  case CMD_FAULT_CLEAR: {
    g_state.fault_flags = 0;
    g_state.sys_state   = SYS_IDLE;
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP0R = 0;   // clear magic so T:201 shows valid=false
    // BKP5R (cumulative fault count) is intentionally preserved
    serial_send_line("{\"T\":210,\"ack\":true}");
    break;
  }

  // ── T:220  Save PID config to flash sector 7 ──────────────────────────────
  case CMD_CONFIG_SAVE: {
    ConfigData cfg = {};
    cfg.kp = g_state.pid_kp;
    cfg.ki = g_state.pid_ki;
    cfg.kd = g_state.pid_kd;
    cfg.kf = g_state.pid_kf;
    bool ok = config_store_save(&cfg);
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"T\":220,\"ok\":%s}", ok ? "true" : "false");
    serial_send_line(buf);
    break;
  }

  // ── T:221  Load config from flash and apply gains ─────────────────────────
  case CMD_CONFIG_LOAD: {
    ConfigData cfg;
    bool ok = config_store_load(&cfg);
    if (ok) {
      g_state.pid_kp = cfg.kp;
      g_state.pid_ki = cfg.ki;
      g_state.pid_kd = cfg.kd;
      g_state.pid_kf = cfg.kf;
      g_state.pid_gains_pending = true;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"T\":221,\"ok\":%s}", ok ? "true" : "false");
    serial_send_line(buf);
    break;
  }

  // ── T:222  Dump current runtime config (no save) ──────────────────────────
  case CMD_CONFIG_DUMP: {
    char buf[160];
    snprintf(buf, sizeof(buf),
      "{\"T\":222,\"kp\":%.4f,\"ki\":%.4f,\"kd\":%.4f,\"kf\":%.4f"
      ",\"state\":%d,\"faults\":\"0x%lX\"}",
      g_state.pid_kp, g_state.pid_ki, g_state.pid_kd, g_state.pid_kf,
      (int)g_state.sys_state, g_state.fault_flags);
    serial_send_line(buf);
    break;
  }

  // ── T:223  Reset config to compile-time defaults ──────────────────────────
  case CMD_CONFIG_RESET: {
    g_state.pid_kp = PID_KP;
    g_state.pid_ki = PID_KI;
    g_state.pid_kd = PID_KD;
    g_state.pid_kf = PID_KF;
    g_state.pid_gains_pending = true;
    serial_send_line("{\"T\":223,\"ack\":true}");
    break;
  }

  // ── T:240  Get current PID gains ──────────────────────────────────────────
  case CMD_PID_GET: {
    char buf[96];
    snprintf(buf, sizeof(buf),
      "{\"T\":240,\"kp\":%.4f,\"ki\":%.4f,\"kd\":%.4f,\"kf\":%.4f}",
      g_state.pid_kp, g_state.pid_ki, g_state.pid_kd, g_state.pid_kf);
    serial_send_line(buf);
    break;
  }

  // ── T:241  Set PID gains — applied by control_task next cycle ─────────────
  // {"T":241,"kp":0.3,"ki":0.8,"kd":0.0,"kf":0.5}
  case CMD_PID_SET: {
    float kp = 0, ki = 0, kd = 0, kf = 0;
    bool ok = parse_json_float(line, "kp", &kp) &&
              parse_json_float(line, "ki", &ki) &&
              parse_json_float(line, "kd", &kd) &&
              parse_json_float(line, "kf", &kf);
    if (ok) {
      g_state.pid_kp = kp;
      g_state.pid_ki = ki;
      g_state.pid_kd = kd;
      g_state.pid_kf = kf;
      g_state.pid_gains_pending = true;
    }
    char buf[96];
    snprintf(buf, sizeof(buf),
      "{\"T\":241,\"ok\":%s,\"kp\":%.4f,\"ki\":%.4f,\"kd\":%.4f,\"kf\":%.4f}",
      ok ? "true" : "false",
      g_state.pid_kp, g_state.pid_ki, g_state.pid_kd, g_state.pid_kf);
    serial_send_line(buf);
    break;
  }

  default:
    break;
  }
}

// ─── Periodic T:1001 chassis feedback ─────────────────────────────────────────
// Called by telemetry_task when continuous_fb is true.
//
// IMU state (roll/pitch/yaw/temp) is NOT re-read here. control_task calls
// imu_update() at 50 Hz and writes g_state.roll/pitch/yaw/imu_temp each cycle.
// Reading cached values avoids I2C bus contention between control_task (prio 9)
// and telemetry_task (prio 2) even though the I2C mutex would protect correctness.
//
// Encoder tick delta (tl / tr):
// g_state.tick_total_left/right are monotonically increasing totals written by
// control_task. This function keeps static "last published" values and subtracts
// to get the delta since the previous T:1001. No reset is needed so there is no
// read-reset race with the higher-priority control_task.
//
// Jetson odometry conversion (ENCODER_COUNTS_PER_REV = 2150):
//   distance_left  = (tl / 2150.0) * 2π * wheel_radius   [metres]
//   distance_right = (tr / 2150.0) * 2π * wheel_radius

void json_cmd_publish_telemetry(void) {
  static uint16_t seq = 0;

  ina219_read(&g_state.battery_voltage, &g_state.battery_current);
  g_state.rpm_left  = encoder_get_left_rpm();
  g_state.rpm_right = encoder_get_right_rpm();

  static int32_t last_tl = 0;
  static int32_t last_tr = 0;
  int32_t tl = g_state.tick_total_left  - last_tl;
  int32_t tr = g_state.tick_total_right - last_tr;
  last_tl = g_state.tick_total_left;
  last_tr = g_state.tick_total_right;

  char buf[480];
  int len = snprintf(buf, sizeof(buf),
    "{\"T\":%d,\"L\":%.1f,\"R\":%.1f"
    ",\"tl\":%ld,\"tr\":%ld"
    ",\"r\":%.1f,\"p\":%.1f,\"y\":%.1f"
    ",\"temp\":%.1f,\"v\":%.2f,\"i\":%.2f",
    FEEDBACK_BASE_INFO,
    g_state.rpm_left, g_state.rpm_right,
    tl, tr,
    g_state.roll, g_state.pitch, g_state.yaw,
    g_state.imu_temp,
    g_state.battery_voltage, g_state.battery_current);

  if (len > 0 && g_state.pid_debug_enabled) {
    len += snprintf(buf + len, sizeof(buf) - (size_t)len,
      ",\"pl\":%.1f,\"pr\":%.1f"
      ",\"ol\":%.1f,\"or\":%.1f"
      ",\"il\":%.2f,\"ir\":%.2f"
      ",\"shl\":%d,\"shr\":%d"
      ",\"sll\":%d,\"slr\":%d"
      ",\"ifl\":%d,\"ifr\":%d",
      g_state.pid_profiled_left, g_state.pid_profiled_right,
      g_state.pid_output_left, g_state.pid_output_right,
      g_state.pid_integrator_left, g_state.pid_integrator_right,
      g_state.pid_sat_high_left ? 1 : 0, g_state.pid_sat_high_right ? 1 : 0,
      g_state.pid_sat_low_left ? 1 : 0, g_state.pid_sat_low_right ? 1 : 0,
      g_state.pid_i_freeze_left ? 1 : 0, g_state.pid_i_freeze_right ? 1 : 0);
  }

  // Sequence ID — Jetson uses this to detect dropped packets
  if (len > 0) {
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, ",\"seq\":%u", seq++);
  }

  // CRC16-CCITT over all bytes written so far (before closing })
  if (len > 0) {
    uint16_t crc = crc16_ccitt((const uint8_t *)buf, (size_t)len);
    len += snprintf(buf + len, sizeof(buf) - (size_t)len, ",\"crc\":%u}", crc);
  }

  if (len > 0) serial_send_line(buf);
}
