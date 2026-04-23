// ─── serial_cmd.cpp — USB CDC serial interface ────────────────────────────────
//
// All communication goes through USB CDC (PA11/PA12 on the Black Pill).
// The USB receive callback (CDC_Receive_FS in usbd_cdc_if.c) writes bytes
// into ring_buf; serial_read_line() consumes them one byte at a time.
// serial_send_line() calls CDC_Transmit_FS() to send back to the host.

#include "serial_cmd.h"
#include "usbd_cdc_if.h"
#include "usbd_desc.h"
#include "usbd_cdc.h"
#include <string.h>

// ─── USB Device handle (referenced by usbd_cdc_if.c) ────────────────────────
USBD_HandleTypeDef hUsbDeviceFS;

// ─── Line assembly buffer ─────────────────────────────────────────────────────
#define SERIAL_BUFFER_SIZE  256

static char   rx_buffer[SERIAL_BUFFER_SIZE];
static size_t rx_index = 0;

// ─── RX ring buffer ───────────────────────────────────────────────────────────
// Written by CDC_Receive_FS (USB ISR context); read by serial_task.
// Single-producer / single-consumer on Cortex-M4 — volatile indices suffice.
// RING_SIZE must be a power of 2; exported so usbd_cdc_if.c can access it.
volatile uint8_t  ring_buf[SERIAL_RING_SIZE];
volatile uint32_t ring_head = 0;   // written by CDC ISR
volatile uint32_t ring_tail = 0;   // written by serial_task

// ─── serial_init — initialise USB CDC ────────────────────────────────────────

void serial_init(void)
{
    rx_index  = 0;
    ring_head = 0;
    ring_tail = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));

    USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS);
    USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC);
    USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS);
    USBD_Start(&hUsbDeviceFS);
}

// ─── serial_read_line — called from serial_task ───────────────────────────────
// Returns:  1 = complete line in buffer
//           0 = byte consumed, line not yet complete
//          -1 = ring buffer empty (caller should yield)

int serial_read_line(char *buffer, size_t buffer_size)
{
    if (ring_head == ring_tail) {
        return -1;
    }

    uint8_t ch = ring_buf[ring_tail];
    ring_tail = (ring_tail + 1u) & (SERIAL_RING_SIZE - 1u);

    if (ch == '\r') { return 0; }   // discard CR

    if (ch == '\n') {
        rx_buffer[rx_index] = '\0';
        strncpy(buffer, rx_buffer, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        rx_index = 0;
        return 1;
    }

    if (rx_index + 1u < SERIAL_BUFFER_SIZE) {
        rx_buffer[rx_index++] = (char)ch;
    } else {
        rx_index = 0;   // overflow — discard and restart
    }
    return 0;
}

// ─── serial_send_line — called by json_cmd.cpp ────────────────────────────────
// Copies the string into a static buffer and transmits via USB CDC.
// Retries briefly if the previous transfer is still in flight.

void serial_send_line(const char *line)
{
    static uint8_t tx_buf[SERIAL_BUFFER_SIZE + 2];

    size_t len = strlen(line);
    if (len > SERIAL_BUFFER_SIZE) len = SERIAL_BUFFER_SIZE;

    memcpy(tx_buf, line, len);
    tx_buf[len]     = '\n';
    tx_buf[len + 1] = '\0';

    // Spin-wait up to ~5 ms if a previous transfer is in flight
    for (int retry = 0; retry < 50; retry++) {
        if (CDC_Transmit_FS(tx_buf, (uint16_t)(len + 1)) == USBD_OK) return;
        HAL_Delay(1);
    }
    // Drop the packet if USB is not connected or persistently busy
}
