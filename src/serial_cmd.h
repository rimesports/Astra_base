#pragma once

#include <stddef.h>
#include "usbd_def.h"

// Ring buffer size shared between serial_cmd.cpp and usbd_cdc_if.c
// Must be a power of 2.
#define SERIAL_RING_SIZE  128u

#ifdef __cplusplus
extern "C" {
#endif

// USB Device handle — defined in serial_cmd.cpp, used by usbd_cdc_if.c
extern USBD_HandleTypeDef hUsbDeviceFS;

void serial_init(void);

// Returns:
//   1  — complete newline-terminated line stored in buffer, ready to dispatch
//   0  — byte consumed from ring buffer, line not yet complete
//  -1  — ring buffer empty (USB idle)
int serial_read_line(char *buffer, size_t buffer_size);

void serial_send_line(const char *line);

#ifdef __cplusplus
}
#endif
