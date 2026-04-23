// ─── usbd_cdc_if.c ────────────────────────────────────────────────────────────
// CDC class interface — bridges the USB Device middleware to the serial ring
// buffer used by serial_cmd.cpp / serial_task.
//
// Receive path:  USB ISR → CDC_Receive_FS() → ring buffer ← serial_read_line()
// Transmit path: serial_write() → CDC_Transmit_FS() → USB ISR → host

#include "usbd_cdc_if.h"
#include <string.h>

// ─── Ring buffer (shared with serial_cmd.cpp) ─────────────────────────────────
// Defined in serial_cmd.cpp; CDC receive callback writes here.
#include "serial_cmd.h"   // SERIAL_RING_SIZE
extern volatile uint8_t  ring_buf[];
extern volatile uint32_t ring_head;
extern volatile uint32_t ring_tail;

// ─── USB Device handle ───────────────────────────────────────────────────────
extern USBD_HandleTypeDef hUsbDeviceFS;   // defined in serial_cmd.cpp

// ─── CDC receive buffer (used by the middleware for IN transfers from host) ──
#define APP_RX_DATA_SIZE  256U
static uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

// ─── Transmit buffer ─────────────────────────────────────────────────────────
#define APP_TX_DATA_SIZE  256U
static uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

// ─── CDC callbacks ───────────────────────────────────────────────────────────

static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    return USBD_OK;
}

static int8_t CDC_DeInit_FS(void)
{
    return USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)cmd; (void)pbuf; (void)length;
    // Accept all control requests (SET_LINE_CODING, SET_CONTROL_LINE_STATE, etc.)
    // without acting on them — baud rate is irrelevant for USB CDC.
    return USBD_OK;
}

static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
    // Put each received byte into the ring buffer so serial_read_line() sees it
    for (uint32_t i = 0; i < *Len; i++) {
        uint32_t next = (ring_head + 1u) & (SERIAL_RING_SIZE - 1u);
        if (next != ring_tail) {
            ring_buf[ring_head] = Buf[i];
            ring_head = next;
        }
    }
    // Re-arm the OUT endpoint for the next packet
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
    (void)Buf; (void)Len; (void)epnum;
    return USBD_OK;
}

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS,
    CDC_TransmitCplt_FS,
};

// ─── Public transmit function ────────────────────────────────────────────────

uint8_t CDC_Transmit_FS(uint8_t *buf, uint16_t len)
{
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hcdc == NULL)          return USBD_FAIL;
    if (hcdc->TxState != 0)   return USBD_BUSY;
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, buf, len);
    return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}
