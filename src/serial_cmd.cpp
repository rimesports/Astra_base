#include "serial_cmd.h"
#include "stm32l4xx_hal.h"
#include <string.h>
#include "main.h"

// ─── Line assembly buffer ──────────────────────────────────────────────────────
#define SERIAL_BUFFER_SIZE  256

static char   rx_buffer[SERIAL_BUFFER_SIZE];
static size_t rx_index = 0;

// ─── Interrupt-driven RX ring buffer ──────────────────────────────────────────
// The ISR writes one byte per interrupt; serial_read_line consumes one byte per
// call.  At 115200 baud, bytes arrive every ~87 µs.  With serial_task sleeping
// 1 ms on the idle path, up to ~11 bytes can queue before the task wakes.
// 128 bytes gives ~11 ms of headroom — 11× the 1 ms yield period.
//
// Single-producer (ISR) / single-consumer (serial_task) on a single-core MCU.
// volatile uint32_t indices are sufficient; no explicit memory barriers needed
// on Cortex-M4 (no weak memory ordering on loads/stores).
#define RING_SIZE  128u   // must be a power of 2

static volatile uint8_t  ring_buf[RING_SIZE];
static volatile uint32_t ring_head = 0;  // written by ISR only
static volatile uint32_t ring_tail = 0;  // written by serial_task only

extern UART_HandleTypeDef huart2;

// ─── serial_init ───────────────────────────────────────────────────────────────

void serial_init(void) {
    rx_index  = 0;
    ring_head = 0;
    ring_tail = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));

    // Enable RXNE interrupt.  On STM32L4, RXNEIE also enables the ORE
    // interrupt source — the ISR clears ORE to prevent spurious re-entry.
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);

    // Priority 6 matches serial_task.  No FreeRTOS FromISR APIs are called
    // from the ISR, so any NVIC priority is permissible.
    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

// ─── serial_usart2_irq_handler — called from USART2_IRQHandler ────────────────

void serial_usart2_irq_handler(void) {
    uint32_t isr = huart2.Instance->ISR;

    if (isr & USART_ISR_RXNE) {
        // Reading RDR clears RXNE automatically.
        uint8_t ch = (uint8_t)(huart2.Instance->RDR & 0xFFu);
        uint32_t next = (ring_head + 1u) & (RING_SIZE - 1u);
        if (next != ring_tail) {        // drop silently only if buffer full
            ring_buf[ring_head] = ch;
            ring_head = next;
        }
    }

    if (isr & USART_ISR_ORE) {
        // Clear overrun error flag; without this the IRQ re-fires immediately.
        huart2.Instance->ICR = USART_ICR_ORECF;
    }
}

// ─── serial_read_line — called from serial_task ────────────────────────────────

int serial_read_line(char *buffer, size_t buffer_size) {
    if (ring_head == ring_tail) {
        return -1;  // ring buffer empty — caller should yield
    }

    uint8_t ch = ring_buf[ring_tail];
    ring_tail = (ring_tail + 1u) & (RING_SIZE - 1u);

    if (ch == '\r') {
        return 0;   // discard CR
    }

    if (ch == '\n') {
        rx_buffer[rx_index] = '\0';
        strncpy(buffer, rx_buffer, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        rx_index = 0;
        return 1;   // complete line ready
    }

    if (rx_index + 1u < SERIAL_BUFFER_SIZE) {
        rx_buffer[rx_index++] = (char)ch;
    } else {
        rx_index = 0;   // overflow — discard and restart
    }
    return 0;   // byte consumed, line not yet complete
}

// ─── serial_send_line ─────────────────────────────────────────────────────────

void serial_send_line(const char *line) {
    size_t length = strlen(line);
    HAL_UART_Transmit(&huart2, (uint8_t *)line, length, 200);
    const char newline[1] = {'\n'};
    HAL_UART_Transmit(&huart2, (uint8_t *)newline, 1, 100);
}
