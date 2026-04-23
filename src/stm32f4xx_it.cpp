// ─── stm32f4xx_it.cpp — Interrupt service routines ───────────────────────────
//
//  PendSV_Handler and SVC_Handler are NOT defined here.
//  FreeRTOSConfig.h maps them via #define so port.c compiles them with the
//  correct CMSIS vector names.
//
//  SysTick_Handler IS defined here — it must call both HAL_IncTick() and
//  xPortSysTickHandler() so both HAL and FreeRTOS stay in sync.

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "usbd_conf.h"   // hpcd_USB_OTG_FS

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

// ─── USB OTG FS ───────────────────────────────────────────────────────────────
void OTG_FS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}

// ─── Encoder EXTI handlers ────────────────────────────────────────────────────
// EXTI3  → ENCODER_LEFT_A  (PB3)
// EXTI4  → ENCODER_LEFT_B  (PB4)
// EXTI9_5 → ENCODER_RIGHT_B (PB5) + ENCODER_RIGHT_A (PB6)

void EXTI3_IRQHandler(void)   { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3); }
void EXTI4_IRQHandler(void)   { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4); }
void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
}

} // extern "C"
