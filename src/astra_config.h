#pragma once

#include <stdint.h>

// Motor driver pins
#define MOTOR_LEFT_PWM_PIN      GPIO_PIN_0
#define MOTOR_LEFT_PWM_PORT     GPIOA
#define MOTOR_LEFT_DIR_PIN      GPIO_PIN_0
#define MOTOR_LEFT_DIR_PORT     GPIOB

#define MOTOR_RIGHT_PWM_PIN     GPIO_PIN_1
#define MOTOR_RIGHT_PWM_PORT    GPIOA
#define MOTOR_RIGHT_DIR_PIN     GPIO_PIN_1
#define MOTOR_RIGHT_DIR_PORT    GPIOB

// Encoder pins
#define ENCODER_LEFT_A_PIN      GPIO_PIN_3
#define ENCODER_LEFT_A_PORT     GPIOB
#define ENCODER_LEFT_B_PIN      GPIO_PIN_4
#define ENCODER_LEFT_B_PORT     GPIOB

#define ENCODER_RIGHT_A_PIN     GPIO_PIN_6  // A/B swapped to match physical direction
#define ENCODER_RIGHT_A_PORT    GPIOB
#define ENCODER_RIGHT_B_PIN     GPIO_PIN_5
#define ENCODER_RIGHT_B_PORT    GPIOB

// I2C pins
#define I2C_SCL_PIN             GPIO_PIN_8
#define I2C_SCL_PORT            GPIOB
#define I2C_SDA_PIN             GPIO_PIN_9
#define I2C_SDA_PORT            GPIOB

// UART pins
#define UART_TX_PIN             GPIO_PIN_2
#define UART_TX_PORT            GPIOA
#define UART_RX_PIN             GPIO_PIN_3
#define UART_RX_PORT            GPIOA

// Motor output ranges
#define MOTOR_PWM_MAX           1000
#define MOTOR_PWM_FREQ_HZ       10000
#define MOTOR_PWM_MAX_SPEED     100

// Encoder calibration
#define ENCODER_PPR_MOTOR       28
#define ENCODER_GEAR_RATIO      19.2f
#define ENCODER_QUADRATURE      4
#define ENCODER_COUNTS_PER_REV  ((int)(ENCODER_PPR_MOTOR * ENCODER_GEAR_RATIO * ENCODER_QUADRATURE))

// Command and telemetry
#define HEARTBEAT_TIMEOUT_MS    3000
#define TELEMETRY_PERIOD_MS     200
#define CONTROL_PERIOD_MS       20

// IMU/Battery
#define MPU6050_I2C_ADDRESS     0x68   // AD0 = GND
#define INA219_I2C_ADDRESS      0x40

// ─── FreeRTOS task priorities (higher number = higher priority) ───────────────
// STM32F411 is single-core — priorities determine preemption order only.
// Safety-critical tasks (future: obstacle detection) should use 10+.
#define TASK_CONTROL_PRIO       9   // motor PID loop — must run on time
#define TASK_SERIAL_PRIO        6   // Jetson UART command handler
#define TASK_TELEMETRY_PRIO     2   // periodic T:1001 feedback — background

// Stack sizes in words (1 word = 4 bytes on ARM)
// control_task runs imu_update() which calls into HAL I2C + xSemaphoreTake;
// the Cortex-M4 FP context save alone is 104 bytes. 512 words gives comfortable
// headroom; check T:200 "stk_ctrl" watermark after bring-up and trim if desired.
#define TASK_CONTROL_STACK      1024 // 4 KB — control loop + HAL I2C + watchdog + fault paths
#define TASK_SERIAL_STACK       512  // 2 KB — JSON string parsing
#define TASK_TELEMETRY_STACK    512  // 2 KB — snprintf + I2C reads

// ─── Velocity PID ────────────────────────────────────────────────────────────
// Motor: goBILDA 5203 Yellow Jacket, 312 RPM @ 12V, 19.2:1 gearbox.
// At 7.4V nominal: ~193 RPM no-load. MOTOR_MAX_RPM sets what target_left=100 means.
// T:1 L=1.0 → target_left=100 → setpoint = MOTOR_MAX_RPM.
#define MOTOR_MAX_RPM           200.0f   // RPM ceiling at 7.4V; tune after bench measurement

// ── Tuning order ──────────────────────────────────────────────────────────────
// 1. Set Kp=Ki=Kd=0. Raise Kf until actual RPM ≈ setpoint open-loop.
//    Kf is the open-loop motor gain: if setpoint=200 RPM needs output≈50, Kf≈0.25.
// 2. Raise Kp until response is crisp but not oscillatory.
// 3. Re-enable Ki to eliminate the remaining steady-state error.
// 4. Kd stays 0 unless RPM is filtered first — derivative amplifies encoder noise
//    at 50 Hz even though derivative-on-measurement avoids setpoint kick.

#define PID_KP                  0.30f
#define PID_KI                  0.80f
#define PID_KD                  0.00f
// Feedforward gain: output contribution = Kf × setpoint_rpm.
// Start near 0.5 (rough mid-range), then tune per step 1 above.
#define PID_KF                  0.50f

// ── Setpoint slew rate (motion profiling) ─────────────────────────────────────
// Limits how fast the RPM setpoint can change per control cycle.
// This replaces a step command with a smooth ramp, which:
//   - reduces wheel slip and mechanical shock on acceleration
//   - keeps yaw-rate correction effective (no instantaneous velocity spikes)
//   - reduces integrator kick when reversing direction
// At 50 Hz (dt=0.02 s): 400 RPM/s → 8 RPM per cycle (~0.04 s to full speed from rest).
// Raise this value if the robot feels sluggish; lower it if wheels slip on start.
#define ACCEL_LIMIT_RPM_PER_S   400.0f

// Yaw-rate correction (MPU-6050 gz, deg/s → PWM correction applied when driving straight).
// Positive gz = CCW rotation (robot turning left). Correction reduces left, increases right.
// If robot corrects the wrong way, negate YAW_CORRECTION_GAIN.
#define YAW_CORRECTION_GAIN     0.40f    // deg/s → PWM units
#define YAW_STRAIGHT_THRESHOLD  15       // apply yaw correction when |target_L - target_R| < this

// ─── JSON T-codes over USB CDC ───────────────────────────────────────────────
#define CMD_STOP                0       // {"T":0} stop both motors immediately
#define CMD_SPEED_CTRL          1       // {"T":1,"L":-0.5..0.5,"R":-0.5..0.5}
#define CMD_PWM_INPUT           11      // {"T":11,"L":-255..255,"R":-255..255}
#define CMD_ROS_CTRL            13      // {"T":13,"linear":x,"angular":z}
#define CMD_IMU_QUERY           126
#define CMD_FEEDBACK_FLOW       130     // {"T":130,"cmd":1}  enable/disable
#define CMD_FEEDBACK_INTERVAL   131     // {"T":131,"cmd":1,"interval":100}
#define CMD_PID_DEBUG           132     // {"T":132,"cmd":1} enable PID debug fields in T:1001
#define CMD_SERIAL_ECHO         143

#define FEEDBACK_BASE_INFO      1001    // T:1001 periodic chassis feedback
#define FEEDBACK_IMU_DATA       1002    // T:1002 IMU snapshot

#define CMD_SYSDIAG             200     // {"T":200} system health check

// ─── IWDG ────────────────────────────────────────────────────────────────────
// LSI ≈ 32 kHz, prescaler /64 → 500 ticks/s → reload 1499 ≈ 3 s timeout.
// control_task feeds it every 20 ms, so normal operation never trips it.
#define IWDG_PRESCALER_VAL  IWDG_PRESCALER_64
#define IWDG_RELOAD_VAL     1499U

// ─── RTC backup register layout (survives watchdog reset) ────────────────────
#define HF_BKP_MAGIC_VAL    0xDEADBEEFU
// BKP0R = magic, BKP1R = PC, BKP2R = LR, BKP3R = CFSR, BKP4R = HFSR,
// BKP5R = cumulative boot-fault counter (cleared on clean boot)

// ─── Flash config storage ────────────────────────────────────────────────────
// Sector 7 (128 KB, 0x08060000) — far from firmware, safe from upload overwrites.
#define CONFIG_FLASH_SECTOR  FLASH_SECTOR_7
#define CONFIG_FLASH_ADDR    0x08060000U
#define CONFIG_MAGIC         0xA57A57A5U
#define CONFIG_VERSION       1U

// ─── Production T-codes (extend existing protocol) ───────────────────────────
#define CMD_HF_DUMP         201    // {"T":201} hardfault register dump
#define CMD_FAULT_CLEAR     210    // {"T":210} clear fault flags, → SYS_IDLE
#define CMD_CONFIG_SAVE     220    // {"T":220} save PID+IMU cal to flash
#define CMD_CONFIG_LOAD     221    // {"T":221} load config from flash and apply
#define CMD_CONFIG_DUMP     222    // {"T":222} print current config (no save)
#define CMD_CONFIG_RESET    223    // {"T":223} reset config to compile-time defaults
#define CMD_PID_GET         240    // {"T":240} get current PID gains
#define CMD_PID_SET         241    // {"T":241,"kp":x,"ki":x,"kd":x,"kf":x}

// ─── Utility ─────────────────────────────────────────────────────────────────
#define CLAMP(x, lo, hi)  ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
