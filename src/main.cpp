// ═════════════════════════════════════════════════════════════════════════════
//  main.cpp — Astra Base STM32L476RG
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
//  Interrupt handlers (SysTick, PendSV, SVC, EXTI) live in stm32l4xx_it.cpp.
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
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

// ─── HAL peripheral handles (extern'd in main.h) ──────────────────────────────
// hi2c1 is defined in i2c_bus.cpp — do not re-define here
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart2;

// ─── Peripheral init prototypes ──────────────────────────────────────────────
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_TIM2_Init(void);
void MX_USART2_UART_Init(void);

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
    MX_USART2_UART_Init();

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
//  control_task — priority 9, 10 ms fixed period
//
//  Uses vTaskDelayUntil so the period is exact regardless of how long the
//  body takes.  This is the real-time spine of the robot.
// ═════════════════════════════════════════════════════════════════════════════

static void control_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (1)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));

        encoder_update();

        uint32_t now = HAL_GetTick();

        // Heartbeat failsafe — stop motors if Jetson goes silent
        if (g_state.command_received &&
            (now - g_state.last_command_ms) > HEARTBEAT_TIMEOUT_MS)
        {
            motor_ctrl_set_speed(0, 0);
            g_state.target_left     = 0;
            g_state.target_right    = 0;
            g_state.pwm_left        = 0;
            g_state.pwm_right       = 0;
            g_state.direct_pwm_mode = false;
            g_state.command_received = false;
        }
        else if (g_state.direct_pwm_mode)
        {
            // T:11 direct PWM: scale -255..+255 → -100..+100
            int16_t sl = (int16_t)((int32_t)g_state.pwm_left  * 100 / 255);
            int16_t sr = (int16_t)((int32_t)g_state.pwm_right * 100 / 255);
            motor_ctrl_set_speed(sl, sr);
        }
        else
        {
            motor_ctrl_set_speed(g_state.target_left, g_state.target_right);
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
//  Called by EXTI handlers in stm32l4xx_it.cpp via HAL_GPIO_EXTI_IRQHandler.
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
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    HAL_PWREx_EnableVddIO2();

    // HSI 16 MHz → PLL → 80 MHz SYSCLK
    // VCO = 16 MHz * N / M = 16 * 20 / 1 = 320 MHz
    // SYSCLK = VCO / R = 320 / 4 = 80 MHz
    // Note: PLLRGE / PLLVCOSEL / PLLFRACN are STM32H7 fields — not present on L476.
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = 1;               // /1 → 16 MHz into VCO
    RCC_OscInitStruct.PLL.PLLN            = 20;              // x20 → 320 MHz VCO
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV7;  // PLLP unused as SYSCLK
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;  // PLLQ (USB, RNG)
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV4;  // /4 → 80 MHz SYSCLK
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);

    PeriphClkInit.PeriphClockSelection  = RCC_PERIPHCLK_USART2 | RCC_PERIPHCLK_I2C1;
    PeriphClkInit.Usart2ClockSelection  = RCC_USART2CLKSOURCE_PCLK1;
    PeriphClkInit.I2c1ClockSelection    = RCC_I2C1CLKSOURCE_PCLK1;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
}

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // Motor direction pins — default low (forward)
    HAL_GPIO_WritePin(MOTOR_LEFT_DIR_PORT,  MOTOR_LEFT_DIR_PIN,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_RIGHT_DIR_PORT, MOTOR_RIGHT_DIR_PIN, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin   = MOTOR_LEFT_DIR_PIN | MOTOR_RIGHT_DIR_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // Motor PWM pins — TIM2 CH1/CH2 alternate function
    GPIO_InitStruct.Pin       = MOTOR_LEFT_PWM_PIN | MOTOR_RIGHT_PWM_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // UART TX/RX — USART2 alternate function
    GPIO_InitStruct.Pin       = UART_TX_PIN | UART_RX_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // I2C SCL/SDA — open-drain with pull-up
    GPIO_InitStruct.Pin       = I2C_SCL_PIN | I2C_SDA_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // BNO055 RST — output, default high (not in reset)
    HAL_GPIO_WritePin(BNO055_RST_PORT, BNO055_RST_PIN, GPIO_PIN_SET);
    GPIO_InitStruct.Pin       = BNO055_RST_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(BNO055_RST_PORT, &GPIO_InitStruct);

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
    htim2.Init.Prescaler         = 7;
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

void MX_USART2_UART_Init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();

    huart2.Instance            = USART2;
    huart2.Init.BaudRate       = 115200;
    huart2.Init.WordLength     = UART_WORDLENGTH_8B;
    huart2.Init.StopBits       = UART_STOPBITS_1;
    huart2.Init.Parity         = UART_PARITY_NONE;
    huart2.Init.Mode           = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling   = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    HAL_UART_Init(&huart2);
}
