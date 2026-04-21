// ═════════════════════════════════════════════════════════════════════════════
//  stm32l4xx_it.cpp — Interrupt service routines
//
//  PendSV_Handler and SVC_Handler are NOT defined here.
//  FreeRTOSConfig.h maps them directly:
//    #define xPortPendSVHandler  PendSV_Handler
//    #define vPortSVCHandler     SVC_Handler
//  so port.c compiles those two handlers with the correct CMSIS vector names.
//
//  SysTick_Handler IS defined here because it must call both:
//    HAL_IncTick()         — keeps HAL_GetTick() / HAL_Delay() working
//    xPortSysTickHandler() — advances the FreeRTOS tick and runs the scheduler
// ═════════════════════════════════════════════════════════════════════════════

#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "serial_cmd.h"

// xPortSysTickHandler is defined in port.c but not declared in any FreeRTOS
// public header — forward-declare it here.
extern "C" void xPortSysTickHandler(void);

extern "C" {

// ─── SysTick — shared between HAL and FreeRTOS ───────────────────────────────
void SysTick_Handler(void)
{
    HAL_IncTick();
#if ( INCLUDE_xTaskGetSchedulerState == 1 )
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
#endif
        xPortSysTickHandler();
#if ( INCLUDE_xTaskGetSchedulerState == 1 )
    }
#endif
}

// ─── USART2 — RX ring buffer ──────────────────────────────────────────────────
void USART2_IRQHandler(void)
{
    serial_usart2_irq_handler();
}

// ─── Encoder EXTI handlers ────────────────────────────────────────────────────
// EXTI3  → ENCODER_LEFT_A  (PB3)
// EXTI4  → ENCODER_LEFT_B  (PB4)
// EXTI9_5 → ENCODER_RIGHT_A (PB5) + ENCODER_RIGHT_B (PB6)

void EXTI3_IRQHandler(void)   { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3); }
void EXTI4_IRQHandler(void)   { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4); }
void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
}

} // extern "C"
