#pragma once

#include "usbd_cdc.h"

#ifdef __cplusplus
extern "C" {
#endif

extern USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;

// Transmit up to len bytes over USB CDC.
// Returns USBD_OK on success, USBD_BUSY if previous transfer still in flight.
uint8_t CDC_Transmit_FS(uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif
