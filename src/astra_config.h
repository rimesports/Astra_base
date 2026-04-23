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
#define TASK_CONTROL_STACK      256  // 1 KB — minimal, no printf
#define TASK_SERIAL_STACK       512  // 2 KB — JSON string parsing
#define TASK_TELEMETRY_STACK    512  // 2 KB — snprintf + I2C reads

// ─── Velocity PID ────────────────────────────────────────────────────────────
// Motor: goBILDA 5203 Yellow Jacket, 312 RPM @ 12V, 19.2:1 gearbox.
// At 7.4V nominal: ~193 RPM no-load. MOTOR_MAX_RPM sets what target_left=100 means.
// T:1 L=1.0 → target_left=100 → setpoint = MOTOR_MAX_RPM.
#define MOTOR_MAX_RPM           200.0f   // RPM ceiling at 7.4V; tune after bench measurement

// Starting gains — expect to tune Kp and Ki on hardware.
// Rule of thumb: increase Kp until oscillation, then back off 30%; set Ki to cancel drift.
// Keep Kd = 0 — derivative on RPM amplifies encoder noise at this sample rate.
#define PID_KP                  0.30f
#define PID_KI                  0.80f
#define PID_KD                  0.00f

// Yaw-rate correction (MPU-6050 gz, deg/s → PWM correction applied when driving straight).
// Positive gz = CCW rotation (robot turning left). Correction reduces left, increases right.
// If robot corrects the wrong way, negate YAW_CORRECTION_GAIN.
#define YAW_CORRECTION_GAIN     0.40f    // deg/s → PWM units
#define YAW_STRAIGHT_THRESHOLD  15       // apply yaw correction when |target_L - target_R| < this

// ─── Waveshare JSON T-codes (same as ESP32 — Jetson sees no difference) ───────
#define CMD_SPEED_CTRL          1       // {"T":1,"L":-0.5..0.5,"R":-0.5..0.5}
#define CMD_PWM_INPUT           11      // {"T":11,"L":-255..255,"R":-255..255}
#define CMD_ROS_CTRL            13      // {"T":13,"linear":x,"angular":z}
#define CMD_IMU_QUERY           126
#define CMD_FEEDBACK_FLOW       130     // {"T":130,"cmd":1}  enable/disable
#define CMD_FEEDBACK_INTERVAL   131     // {"T":131,"cmd":1,"interval":100}
#define CMD_SERIAL_ECHO         143

#define FEEDBACK_BASE_INFO      1001    // T:1001 periodic chassis feedback
#define FEEDBACK_IMU_DATA       1002    // T:1002 IMU snapshot

#define CMD_SYSDIAG             200     // {"T":200} system health check

// ─── Utility ─────────────────────────────────────────────────────────────────
#define CLAMP(x, lo, hi)  ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
