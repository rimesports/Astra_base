#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void serial_init(void);

// Returns:
//   1  — complete newline-terminated line stored in buffer, ready to dispatch
//   0  — byte consumed from ring buffer, line not yet complete
//  -1  — ring buffer empty (UART idle)
int serial_read_line(char *buffer, size_t buffer_size);

void serial_send_line(const char *line);

// Called from USART2_IRQHandler in stm32l4xx_it.cpp.
// Reads one byte from RDR into the ring buffer and clears ORE if set.
void serial_usart2_irq_handler(void);

#ifdef __cplusplus
}
#endif
