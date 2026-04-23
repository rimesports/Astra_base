#pragma once

#include "usbd_cdc.h"

#ifdef __cplusplus
extern "C" {
#endif

extern USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;

// Queue up to len bytes for USB CDC transmission.
// Returns USBD_OK on success, USBD_BUSY if the queue is full.
uint8_t CDC_Transmit_FS(uint8_t *buf, uint16_t len);
uint8_t CDC_IsConfigured_FS(void);
uint8_t CDC_IsPortOpen_FS(void);
uint32_t CDC_GetTxDropped_FS(void);
uint32_t CDC_GetRxDropped_FS(void);
uint32_t CDC_GetTxQueued_FS(void);
uint32_t CDC_GetRxQueued_FS(void);

#ifdef __cplusplus
}
#endif
