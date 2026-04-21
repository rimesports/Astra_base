#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  FreeRTOS configuration — STM32L476RG @ 80 MHz, Cortex-M4F
//
//  Tuned for a robotics application:
//    - 1 ms tick (1 kHz)
//    - Preemptive scheduling
//    - Stack overflow detection (mode 2 — pattern check)
//    - heap_4 allocator (coalescing free, no external fragmentation risk)
//    - No software timers, no co-routines (not needed here)
// ═════════════════════════════════════════════════════════════════════════════

// ─── Scheduler ───────────────────────────────────────────────────────────────
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1   // uses CLZ instruction (CM4F)
#define configUSE_TICKLESS_IDLE                 0   // keep tick running for HAL

// ─── Clock & tick ────────────────────────────────────────────────────────────
#define configCPU_CLOCK_HZ                      80000000UL
#define configTICK_RATE_HZ                      1000        // 1 ms tick

// ─── Tasks ───────────────────────────────────────────────────────────────────
#define configMAX_PRIORITIES                    12
#define configMINIMAL_STACK_SIZE                128         // words (512 bytes)
#define configMAX_TASK_NAME_LEN                 12
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1

// ─── Heap ────────────────────────────────────────────────────────────────────
// STM32L476RG has 128 KB SRAM. 20 KB for FreeRTOS heap leaves plenty for
// stack, globals, and future tasks (safety, collector, sensor).
#define configTOTAL_HEAP_SIZE                   ( 20 * 1024 )
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

// ─── Synchronisation primitives ──────────────────────────────────────────────
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0

// ─── Hooks and diagnostics ───────────────────────────────────────────────────
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2   // pattern-based check
#define configUSE_MALLOC_FAILED_HOOK            1

// ─── Tracing / stats (disable for production) ────────────────────────────────
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0

// ─── Software timers (not used) ──────────────────────────────────────────────
#define configUSE_TIMERS                        0

// ─── Co-routines (deprecated, not used) ──────────────────────────────────────
#define configUSE_CO_ROUTINES                   0

// ─── Interrupt priorities (STM32 NVIC, 4-bit priority register) ──────────────
//
//  NVIC_PRIORITYGROUP_4: 4 bits preempt, 0 bits sub-priority → priorities 0–15
//
//  Rule: any ISR that calls a FreeRTOS "FromISR" API must have its NVIC
//  preempt priority NUMERICALLY >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
//  (i.e. lower urgency than the kernel).
//
//  Our encoder EXTI ISRs only write volatile counters — no FreeRTOS API —
//  so they may run at any priority.
//
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

// Shift to 8-bit register format used by ARM CMSIS NVIC functions
#define configKERNEL_INTERRUPT_PRIORITY \
        ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - 4) )      // 0xF0
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
        ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - 4) ) // 0x50

// ─── Interrupt vector name mapping ───────────────────────────────────────────
// port.c compiles its ISRs with the names below, making them land in the ARM
// vector table automatically.  SysTick is NOT remapped here because we define
// SysTick_Handler ourselves in stm32l4xx_it.cpp to also call HAL_IncTick().
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler

// ─── INCLUDE_ — enable only what is used ─────────────────────────────────────
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1   // useful for tuning stack sizes
