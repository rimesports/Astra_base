// --- usbd_cdc_if.c -----------------------------------------------------------
// CDC class interface between ST's USB Device middleware and the application's
// line-based serial transport.
//
// Receive path:  USB ISR -> CDC_Receive_FS() -> ring buffer -> serial_read_line()
// Transmit path: serial_send_line() -> CDC_Transmit_FS() -> USB ISR -> host

#include "usbd_cdc_if.h"
#include "serial_cmd.h"
#include "usbd_ctlreq.h"
#include <string.h>

extern volatile uint8_t  ring_buf[];
extern volatile uint32_t ring_head;
extern volatile uint32_t ring_tail;
extern USBD_HandleTypeDef hUsbDeviceFS;

#define APP_RX_DATA_SIZE   256U
#define APP_TX_DATA_SIZE  1024U
#define CDC_TX_CHUNK_SIZE   256U

static uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
static uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];
static USBD_CDC_LineCodingTypeDef LineCoding = {
    115200U,
    0x00U,
    0x00U,
    0x08U
};

static volatile uint32_t tx_head = 0;
static volatile uint32_t tx_tail = 0;
static volatile uint32_t tx_in_flight = 0;
static volatile uint32_t tx_drop_count = 0;
static volatile uint32_t rx_drop_count = 0;
static volatile uint8_t cdc_port_open = 0;

static inline uint32_t irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static inline void irq_restore(uint32_t primask)
{
    if (primask == 0U) {
        __enable_irq();
    }
}

static inline uint32_t tx_used_locked(uint32_t head, uint32_t tail)
{
    if (head >= tail) {
        return head - tail;
    }
    return APP_TX_DATA_SIZE - tail + head;
}

static inline uint32_t tx_free_locked(uint32_t head, uint32_t tail)
{
    return (APP_TX_DATA_SIZE - 1U) - tx_used_locked(head, tail);
}

static void CDC_KickTransmit_FS(void)
{
    USBD_CDC_HandleTypeDef *hcdc;
    uint32_t primask;
    uint32_t head;
    uint32_t tail;
    uint32_t used;
    uint32_t chunk;

    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
        return;
    }

    hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hcdc == NULL || hcdc->TxState != 0U) {
        return;
    }

    primask = irq_save();
    if (tx_in_flight != 0U) {
        irq_restore(primask);
        return;
    }

    head = tx_head;
    tail = tx_tail;
    used = tx_used_locked(head, tail);
    if (used == 0U) {
        irq_restore(primask);
        return;
    }

    chunk = APP_TX_DATA_SIZE - tail;
    if (chunk > used) {
        chunk = used;
    }
    if (chunk > CDC_TX_CHUNK_SIZE) {
        chunk = CDC_TX_CHUNK_SIZE;
    }
    tx_in_flight = chunk;
    irq_restore(primask);

    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, &UserTxBufferFS[tail], (uint16_t)chunk);
    if (USBD_CDC_TransmitPacket(&hUsbDeviceFS) != USBD_OK) {
        primask = irq_save();
        tx_in_flight = 0U;
        irq_restore(primask);
    }
}

static int8_t CDC_Init_FS(void)
{
    uint32_t primask = irq_save();
    tx_head = 0U;
    tx_tail = 0U;
    tx_in_flight = 0U;
    tx_drop_count = 0U;
    rx_drop_count = 0U;
    cdc_port_open = 0U;
    irq_restore(primask);

    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0U);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    return USBD_OK;
}

static int8_t CDC_DeInit_FS(void)
{
    uint32_t primask = irq_save();
    tx_head = 0U;
    tx_tail = 0U;
    tx_in_flight = 0U;
    cdc_port_open = 0U;
    irq_restore(primask);
    return USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    switch (cmd) {
    case CDC_SET_LINE_CODING:
        if (length >= 7U) {
            LineCoding.bitrate = (uint32_t)pbuf[0]
                | ((uint32_t)pbuf[1] << 8)
                | ((uint32_t)pbuf[2] << 16)
                | ((uint32_t)pbuf[3] << 24);
            LineCoding.format = pbuf[4];
            LineCoding.paritytype = pbuf[5];
            LineCoding.datatype = pbuf[6];
        }
        break;

    case CDC_GET_LINE_CODING:
        pbuf[0] = (uint8_t)(LineCoding.bitrate);
        pbuf[1] = (uint8_t)(LineCoding.bitrate >> 8);
        pbuf[2] = (uint8_t)(LineCoding.bitrate >> 16);
        pbuf[3] = (uint8_t)(LineCoding.bitrate >> 24);
        pbuf[4] = LineCoding.format;
        pbuf[5] = LineCoding.paritytype;
        pbuf[6] = LineCoding.datatype;
        break;

    case CDC_SET_CONTROL_LINE_STATE: {
        const USBD_SetupReqTypedef *req = (const USBD_SetupReqTypedef *)pbuf;
        uint32_t primask = irq_save();
        cdc_port_open = (req->wValue & 0x0001U) ? 1U : 0U;
        if (cdc_port_open == 0U) {
            tx_head = 0U;
            tx_tail = 0U;
            tx_in_flight = 0U;
        }
        irq_restore(primask);
        if (cdc_port_open != 0U) {
            CDC_KickTransmit_FS();
        }
        break;
    }

    case CDC_SEND_BREAK:
    default:
        break;
    }

    return USBD_OK;
}

static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
    uint8_t dropped = 0U;

    for (uint32_t i = 0; i < *Len; i++) {
        uint32_t next = (ring_head + 1U) & (SERIAL_RING_SIZE - 1U);
        if (next != ring_tail) {
            ring_buf[ring_head] = Buf[i];
            ring_head = next;
        } else {
            dropped = 1U;
        }
    }

    if (dropped != 0U) {
        rx_drop_count++;
    }

    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

static int8_t CDC_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
    uint32_t primask;
    uint32_t sent = (Len != NULL) ? *Len : 0U;

    (void)Buf;
    (void)epnum;

    primask = irq_save();
    if (tx_in_flight != 0U) {
        if (sent == 0U || sent > tx_in_flight) {
            sent = tx_in_flight;
        }
        tx_tail = (tx_tail + sent) % APP_TX_DATA_SIZE;
        tx_in_flight = 0U;
    }
    irq_restore(primask);

    CDC_KickTransmit_FS();
    return USBD_OK;
}

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS,
    CDC_TransmitCplt_FS,
};

uint8_t CDC_Transmit_FS(uint8_t *buf, uint16_t len)
{
    uint32_t primask;
    uint32_t head;
    uint32_t tail;
    uint32_t free_space;
    uint32_t first_len;
    uint32_t second_len;

    if (buf == NULL || len == 0U) {
        return USBD_OK;
    }

    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
        tx_drop_count++;
        return USBD_FAIL;
    }

    if (len >= APP_TX_DATA_SIZE) {
        tx_drop_count++;
        return USBD_FAIL;
    }

    primask = irq_save();
    head = tx_head;
    tail = tx_tail;
    free_space = tx_free_locked(head, tail);
    if ((uint32_t)len > free_space) {
        tx_drop_count++;
        irq_restore(primask);
        return USBD_BUSY;
    }

    first_len = APP_TX_DATA_SIZE - head;
    if (first_len > len) {
        first_len = len;
    }
    second_len = (uint32_t)len - first_len;

    memcpy(&UserTxBufferFS[head], buf, first_len);
    if (second_len > 0U) {
        memcpy(UserTxBufferFS, &buf[first_len], second_len);
    }
    tx_head = (head + (uint32_t)len) % APP_TX_DATA_SIZE;
    irq_restore(primask);

    CDC_KickTransmit_FS();
    return USBD_OK;
}

uint8_t CDC_IsConfigured_FS(void)
{
    return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) ? 1U : 0U;
}

uint8_t CDC_IsPortOpen_FS(void)
{
    return cdc_port_open;
}

uint32_t CDC_GetTxDropped_FS(void)
{
    return tx_drop_count;
}

uint32_t CDC_GetRxDropped_FS(void)
{
    return rx_drop_count;
}

uint32_t CDC_GetTxQueued_FS(void)
{
    uint32_t primask = irq_save();
    uint32_t queued = tx_used_locked(tx_head, tx_tail);
    irq_restore(primask);
    return queued;
}

uint32_t CDC_GetRxQueued_FS(void)
{
    uint32_t primask = irq_save();
    uint32_t queued;
    if (ring_head >= ring_tail) {
        queued = ring_head - ring_tail;
    } else {
        queued = SERIAL_RING_SIZE - ring_tail + ring_head;
    }
    irq_restore(primask);
    return queued;
}
