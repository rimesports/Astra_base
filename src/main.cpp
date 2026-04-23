// ═════════════════════════════════════════════════════════════════════════════
//  main.cpp — Astra Base STM32F411CEU6 (WeAct Black Pill V3.1)
//
//  Three FreeRTOS tasks:
//
//   control_task   (priority 9, 10 ms period)
//     Encoder update → heartbeat failsafe → motor output.
//     Runs at the highest priority so the control loop is never starved.
//
//   serial_task    (priority 6, event-driven)
//     Blocks on UART reads, dispatches JSON T-codes from the Jetson.
//     Writes to g_state; control_task reads from it one tick later.
//
//   telemetry_task (priority 2, fb_interval_ms period)
//     Sends periodic T:1001 feedback when continuous_fb is enabled.
//     Lowest priority — can be delayed without affecting robot behaviour.
//
//  Interrupt handlers (SysTick, PendSV, SVC, EXTI) live in stm32f4xx_it.cpp.
// ═════════════════════════════════════════════════════════════════════════════

#include "main.h"
#include "astra_config.h"
#include "shared_state.h"
#include "motor_ctrl.h"
#include "encoder.h"
#include "i2c_bus.h"
#include "imu.h"
#include "ina219.h"
#include "serial_cmd.h"
#include "json_cmd.h"
#include "pid.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

// ─── HAL peripheral handles (extern'd in main.h) ──────────────────────────────
// hi2c1 is defined in i2c_bus.cpp — do not re-define here
TIM_HandleTypeDef htim2;

// ─── Peripheral init prototypes ──────────────────────────────────────────────
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_TIM2_Init(void);

// ─── FreeRTOS task prototypes ─────────────────────────────────────────────────
static void control_task(void *arg);
static void serial_task(void *arg);
static void telemetry_task(void *arg);

// ─── FreeRTOS safety hooks ────────────────────────────────────────────────────
// Both hooks stop the motors immediately and halt so a debugger can attach.

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    motor_ctrl_set_speed(0, 0);   // safe state first
    __disable_irq();
    while (1) {}
}

extern "C" void vApplicationMallocFailedHook(void)
{
    motor_ctrl_set_speed(0, 0);
    __disable_irq();
    while (1) {}
}

// ═════════════════════════════════════════════════════════════════════════════
//  main
// ═════════════════════════════════════════════════════════════════════════════

int main(void)
{
    // HAL + clocks
    HAL_Init();
    SystemClock_Config();

    // Peripheral hardware init — must complete before tasks start
    MX_GPIO_Init();
    MX_TIM2_Init();
    // USB CDC is initialised inside serial_init() via USBD_Init/Start.
    // USART2 is no longer used — communication is over USB-C (PA11/PA12).

    // Module init
    shared_state_init();
    i2c_bus_init();       // shared I2C — must precede ina219 and imu
    motor_ctrl_init();
    encoder_init();
    imu_init();
    ina219_init();
    serial_init();

    // Create tasks — scheduler not running yet, so these just enqueue
    xTaskCreate(control_task,   "ctrl",  TASK_CONTROL_STACK,   NULL, TASK_CONTROL_PRIO,   NULL);
    xTaskCreate(serial_task,    "ser",   TASK_SERIAL_STACK,    NULL, TASK_SERIAL_PRIO,    NULL);
    xTaskCreate(telemetry_task, "telem", TASK_TELEMETRY_STACK, NULL, TASK_TELEMETRY_PRIO, NULL);

    // Hand control to FreeRTOS — never returns
    vTaskStartScheduler();

    // Unreachable; silences compiler warning
    while (1) {}
}

// ═════════════════════════════════════════════════════════════════════════════
//  control_task — priority 9, CONTROL_PERIOD_MS fixed period
//
//  Velocity PID loop for left and right motors.
//
//  T:1 / T:13 commands set target_left / target_right in ±100 units.
//  These are converted to RPM setpoints (±MOTOR_MAX_RPM).  Before reaching
//  the PID the setpoint is passed through a slew rate limiter that caps
//  acceleration to ACCEL_LIMIT_RPM_PER_S.  The PID then closes the loop
//  using encoder_get_left/right_rpm() as the measurement.
//  The PID output (±100 PWM units) drives motor_ctrl_set_speed().
//
//  T:11 (direct PWM) bypasses the PID entirely — useful for open-loop testing.
//
//  PID improvements over a basic textbook implementation:
//
//  1. Feedforward (Kf):
//     output += Kf × setpoint_rpm — handles the bulk of the steady-state load
//     open-loop so the integrator only corrects disturbances and model error.
//
//  2. Setpoint slew rate limiter:
//     Ramps the RPM setpoint at ≤ ACCEL_LIMIT_RPM_PER_S instead of stepping
//     it.  Prevents wheel slip on hard commands, reduces integrator kick on
//     direction reversal, and keeps yaw correction effective during acceleration.
//     profiled_l / profiled_r are reset to 0 alongside the PIDs on mode switch.
//
//  3. Derivative on measurement:
//     derivative = −Δmeasurement / dt instead of Δerror / dt.
//     A step setpoint change spikes Δerror but the motor cannot accelerate
//     instantly, so Δmeasurement stays smooth — no "derivative kick".
//
//  4. Saturation-aware anti-windup:
//     Integrator is frozen when the output is already at its limit AND the
//     error still pushes further into saturation.  Prevents the integrator
//     from charging against a saturated actuator, which causes overshoot when
//     the motor finally catches up.  A hard clamp remains as a safety bound.
//
//  IMU update:
//  imu_update() is called here at 50 Hz so the complementary filter runs at
//  a consistent rate. Telemetry and on-demand queries read the cached values
//  via imu_get_*() without re-triggering an I2C read.
//
//  Yaw-rate correction:
//  When driving approximately straight (|target_L - target_R| < threshold),
//  gyro Z (gz_dps) is used to counteract rotation: a positive gz (CCW / left
//  turn) reduces left PWM and increases right PWM. If the robot corrects the
//  wrong way, negate YAW_CORRECTION_GAIN in astra_config.h.
// ═════════════════════════════════════════════════════════════════════════════

static void control_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    static PIDController pid_left;
    static PIDController pid_right;
    pid_init(&pid_left,  PID_KP, PID_KI, PID_KD, PID_KF, -100.0f, 100.0f);
    pid_init(&pid_right, PID_KP, PID_KI, PID_KD, PID_KF, -100.0f, 100.0f);

    // Profiled (slew-rate-limited) setpoints — track the commanded RPM but
    // ramp at ≤ ACCEL_LIMIT_RPM_PER_S.  Static so they persist across cycles.
    static float profiled_l = 0.0f;
    static float profiled_r = 0.0f;

    const float dt = CONTROL_PERIOD_MS * 0.001f;   // seconds — fixed period for PID

    while (1)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

        encoder_update();
        imu_update();   // complementary filter needs consistent dt; results cached in imu_get_*()

        // Publish fresh IMU state for telemetry task to read
        g_state.roll      = imu_get_roll();
        g_state.pitch     = imu_get_pitch();
        g_state.yaw       = imu_get_yaw();
        g_state.imu_temp  = imu_get_temp();

        uint32_t now = HAL_GetTick();

        // ── Heartbeat failsafe ───────────────────────────────────────────────
        // Stop motors and reset PID state if Jetson goes silent.
        if (g_state.command_received &&
            (now - g_state.last_command_ms) > HEARTBEAT_TIMEOUT_MS)
        {
            motor_ctrl_set_speed(0, 0);
            g_state.target_left      = 0;
            g_state.target_right     = 0;
            g_state.pwm_left         = 0;
            g_state.pwm_right        = 0;
            g_state.direct_pwm_mode  = false;
            g_state.command_received = false;
            pid_reset(&pid_left);
            pid_reset(&pid_right);
            profiled_l = 0.0f;
            profiled_r = 0.0f;
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
        // ── T:11 direct PWM — bypass PID ────────────────────────────────────
        else if (g_state.direct_pwm_mode)
        {
            // Scale -255..+255 → -100..+100 and apply directly.
            // Reset PIDs and profiled setpoints so there is no integrator
            // wind-up or slew-limiter lag surprise on next switch to velocity mode.
            int16_t sl = (int16_t)((int32_t)g_state.pwm_left  * 100 / 255);
            int16_t sr = (int16_t)((int32_t)g_state.pwm_right * 100 / 255);
            motor_ctrl_set_speed(sl, sr);
            pid_reset(&pid_left);
            pid_reset(&pid_right);
            profiled_l = 0.0f;
            profiled_r = 0.0f;
            g_state.pid_profiled_left = 0.0f;
            g_state.pid_profiled_right = 0.0f;
            g_state.pid_output_left = (float)sl;
            g_state.pid_output_right = (float)sr;
            g_state.pid_integrator_left = 0.0f;
            g_state.pid_integrator_right = 0.0f;
            g_state.pid_sat_high_left = false;
            g_state.pid_sat_high_right = false;
            g_state.pid_sat_low_left = false;
            g_state.pid_sat_low_right = false;
            g_state.pid_i_freeze_left = false;
            g_state.pid_i_freeze_right = false;
        }
        // ── Velocity PID — T:1 / T:13 ───────────────────────────────────────
        else
        {
            // Convert ±100 target to ±RPM setpoint
            const float scale    = MOTOR_MAX_RPM / 100.0f;
            float target_l       = (float)g_state.target_left  * scale;
            float target_r       = (float)g_state.target_right * scale;

            // ── Setpoint slew rate limiter ───────────────────────────────────
            // Cap how fast the setpoint can change each cycle.
            // This turns a step command into a smooth ramp, preventing wheel
            // slip on hard acceleration and reducing integrator kick on reversal.
            const float max_delta = ACCEL_LIMIT_RPM_PER_S * dt;
            profiled_l += CLAMP(target_l - profiled_l, -max_delta, max_delta);
            profiled_r += CLAMP(target_r - profiled_r, -max_delta, max_delta);

            float out_l = pid_compute(&pid_left,  profiled_l, encoder_get_left_rpm(),  dt);
            float out_r = pid_compute(&pid_right, profiled_r, encoder_get_right_rpm(), dt);

            // ── Yaw-rate correction ──────────────────────────────────────────
            // Only correct when driving approximately straight.  When the user
            // commands a deliberate turn the targets differ significantly and
            // correction would fight the intended yaw.
            int16_t tdiff = g_state.target_left - g_state.target_right;
            if (tdiff < 0) tdiff = -tdiff;
            if (tdiff < YAW_STRAIGHT_THRESHOLD)
            {
                // gz_dps > 0  →  CCW rotation (turning left)
                // Correct by reducing left and increasing right PWM output.
                float corr = YAW_CORRECTION_GAIN * imu_get_gz();
                out_l -= corr;
                out_r += corr;
            }

            int16_t cmd_l = (int16_t)CLAMP(out_l, -100.0f, 100.0f);
            int16_t cmd_r = (int16_t)CLAMP(out_r, -100.0f, 100.0f);
            motor_ctrl_set_speed(cmd_l, cmd_r);
            g_state.pid_profiled_left = profiled_l;
            g_state.pid_profiled_right = profiled_r;
            g_state.pid_output_left = pid_left.last_output;
            g_state.pid_output_right = pid_right.last_output;
            g_state.pid_integrator_left = pid_left.integrator;
            g_state.pid_integrator_right = pid_right.integrator;
            g_state.pid_sat_high_left = pid_left.last_saturated_high != 0;
            g_state.pid_sat_high_right = pid_right.last_saturated_high != 0;
            g_state.pid_sat_low_left = pid_left.last_saturated_low != 0;
            g_state.pid_sat_low_right = pid_right.last_saturated_low != 0;
            g_state.pid_i_freeze_left = pid_left.last_integrator_frozen != 0;
            g_state.pid_i_freeze_right = pid_right.last_integrator_frozen != 0;
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  serial_task — priority 6, event-driven
//
//  serial_read_line() does a zero-timeout HAL_UART_Receive (returns in ~1 µs
//  if no byte is waiting).  The tri-state return lets the task distinguish
//  three conditions:
//
//    1  complete line → dispatch immediately, no yield
//    0  byte buffered → loop back immediately, mid-command latency matters
//   -1  idle (no byte) → yield for 1 ms so lower-priority tasks can run
//
//  Yielding only on true idle means a `stop` command (or any command) is
//  never delayed by a spurious 1 ms sleep mid-receive.
// ═════════════════════════════════════════════════════════════════════════════

static void serial_task(void *arg)
{
    (void)arg;
    static char line[256];

    while (1)
    {
        int result = serial_read_line(line, sizeof(line));
        if (result == 1)
        {
            json_cmd_process_line(line);
        }
        else if (result == -1)
        {
            // Truly idle — yield so telemetry_task (priority 2) can run.
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        // result == 0: byte buffered, loop immediately for next byte
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  telemetry_task — priority 2, fb_interval_ms period
//
//  Sends T:1001 chassis feedback to the Jetson.  Rate and enable flag are
//  configurable at runtime via T:130 / T:131 from the Jetson.
// ═════════════════════════════════════════════════════════════════════════════

static void telemetry_task(void *arg)
{
    (void)arg;

    while (1)
    {
        if (g_state.continuous_fb)
        {
            json_cmd_publish_telemetry();
        }
        // Always sleep for fb_interval_ms even when disabled — keeps the
        // task from spinning and burning CPU when feedback is turned off.
        vTaskDelay(pdMS_TO_TICKS(g_state.fb_interval_ms));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  HAL_GPIO_EXTI_Callback
//  Called by EXTI handlers in stm32f4xx_it.cpp via HAL_GPIO_EXTI_IRQHandler.
// ═════════════════════════════════════════════════════════════════════════════

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    encoder_exti_callback(GPIO_Pin);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Peripheral initialisation
// ═════════════════════════════════════════════════════════════════════════════

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // HSI 16 MHz → PLL → 96 MHz SYSCLK + 48 MHz USB
    // PLL input = HSI / M = 16 / 8 = 2 MHz
    // VCO       = 2 * N   = 2 * 192 = 384 MHz  (≤ 432 MHz — OK)
    // SYSCLK    = VCO / P = 384 / 4 = 96 MHz
    // USB48     = VCO / Q = 384 / 8 = 48 MHz   (required exact value for USB)
    //
    // Flash latency 3 WS required for SYSCLK > 90 MHz @ 3.3 V (RM0383 Table 6)
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = 8;    // /8  → 2 MHz PLL input
    RCC_OscInitStruct.PLL.PLLN            = 192;  // ×192 → 384 MHz VCO
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV4;  // /4 → 96 MHz SYSCLK
    RCC_OscInitStruct.PLL.PLLQ            = 8;    // /8 → 48 MHz USB clock
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    // APB1 max = 50 MHz on F411 → divide HCLK by 2 → APB1 = 48 MHz
    // TIM2 clock = 2 × APB1 = 96 MHz (hardware doubles when APB prescaler ≠ 1)
    // TIM2 PSC=9 → timer clock 9.6 MHz; ARR=1000 → PWM = 9.6 kHz (fine for DC motors)
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;   // HCLK  = 96 MHz
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;     // APB1  = 48 MHz
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;     // APB2  = 96 MHz
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3);
}

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Motor direction pins — default low (forward)
    HAL_GPIO_WritePin(MOTOR_LEFT_DIR_PORT,  MOTOR_LEFT_DIR_PIN,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_RIGHT_DIR_PORT, MOTOR_RIGHT_DIR_PIN, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = MOTOR_LEFT_DIR_PIN | MOTOR_RIGHT_DIR_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);   // B0=DIR_L, B1=DIR_R (F411 Black Pill)

    // Motor PWM pins — TIM2 CH1/CH2 alternate function
    GPIO_InitStruct.Pin       = MOTOR_LEFT_PWM_PIN | MOTOR_RIGHT_PWM_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // I2C SCL/SDA — open-drain with pull-up
    GPIO_InitStruct.Pin       = I2C_SCL_PIN | I2C_SDA_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Encoder quadrature inputs — EXTI both edges, pull-up
    GPIO_InitStruct.Pin  = ENCODER_LEFT_A_PIN  | ENCODER_LEFT_B_PIN |
                           ENCODER_RIGHT_A_PIN | ENCODER_RIGHT_B_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Encoder EXTI priorities must be >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
    // (5) if they ever call FreeRTOS FromISR APIs.  Currently they do not,
    // but 5 is safe for future changes.
    HAL_NVIC_SetPriority(EXTI3_IRQn,   5, 0);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
    HAL_NVIC_SetPriority(EXTI4_IRQn,   5, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void MX_TIM2_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 9;   // TIM2 clk = 100 MHz; /10 → 10 MHz; /ARR(1000) → 10 kHz PWM
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = MOTOR_PWM_MAX;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim2);

    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2);
}
