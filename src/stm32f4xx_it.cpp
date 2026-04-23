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
extern "C" volatile uint32_t g_hf_sp = 0;
extern "C" volatile uint32_t g_hf_r0 = 0;
extern "C" volatile uint32_t g_hf_r1 = 0;
extern "C" volatile uint32_t g_hf_r2 = 0;
extern "C" volatile uint32_t g_hf_r3 = 0;
extern "C" volatile uint32_t g_hf_r12 = 0;
extern "C" volatile uint32_t g_hf_lr = 0;
extern "C" volatile uint32_t g_hf_pc = 0;
extern "C" volatile uint32_t g_hf_xpsr = 0;
extern "C" volatile uint32_t g_hf_cfsr = 0;
extern "C" volatile uint32_t g_hf_hfsr = 0;
extern "C" volatile uint32_t g_hf_bfar = 0;
extern "C" volatile uint32_t g_hf_mmfar = 0;

extern "C" void hardfault_capture(uint32_t *stack)
{
    g_hf_sp    = (uint32_t)stack;
    g_hf_r0    = stack[0];
    g_hf_r1    = stack[1];
    g_hf_r2    = stack[2];
    g_hf_r3    = stack[3];
    g_hf_r12   = stack[4];
    g_hf_lr    = stack[5];
    g_hf_pc    = stack[6];
    g_hf_xpsr  = stack[7];
    g_hf_cfsr  = SCB->CFSR;
    g_hf_hfsr  = SCB->HFSR;
    g_hf_bfar  = SCB->BFAR;
    g_hf_mmfar = SCB->MMFAR;

    __disable_irq();
    while (1) {}
}

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

void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4        \n"
        "ite eq            \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "b hardfault_capture\n"
    );
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
