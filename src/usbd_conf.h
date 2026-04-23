#pragma once

// ─── USB Device middleware configuration ─────────────────────────────────────
// usbd_conf.h — hardware-level glue between STM32 HAL and the USB Device stack
//
// IMPORTANT: usbd_def.h uses #ifndef guards for all its defaults.
// All USBD_* config macros MUST be defined BEFORE including usbd_def.h so
// that the #ifndef guards in usbd_def.h see our values and skip their defaults.

// ─── Descriptor limits (must precede #include "usbd_def.h") ─────────────────
#define USBD_MAX_NUM_INTERFACES        2U   // CDC = comm interface + data interface
#define USBD_MAX_NUM_CONFIGURATION     1U
#define USBD_MAX_STR_DESC_SIZ        512U
#define USBD_SUPPORT_USER_STRING_DESC  0U
#define USBD_SELF_POWERED              1U
#define USBD_MAX_POWER                50U   // mA (from USB host)

// ─── Debug (disabled) ────────────────────────────────────────────────────────
#define USBD_DEBUG_LEVEL  0U

#include "stm32f4xx_hal.h"
#include "usbd_def.h"
#include <stdlib.h>
#include <string.h>

// ─── Endpoint addresses ───────────────────────────────────────────────────────
#define CDC_IN_EP   0x81U   // EP1 IN  — data device→host
#define CDC_OUT_EP  0x01U   // EP1 OUT — data host→device
#define CDC_CMD_EP  0x82U   // EP2 IN  — CDC control notifications

// ─── Packet sizes ─────────────────────────────────────────────────────────────
#define CDC_DATA_FS_MAX_PACKET_SIZE  64U
#define CDC_CMD_PACKET_SIZE           8U
#define USB_FS_MAX_PACKET_SIZE       64U
#define USB_MAX_EP0_SIZE             64U

// ─── Memory management — standard heap ───────────────────────────────────────
#define USBD_malloc   malloc
#define USBD_free     free
#define USBD_memset   memset
#define USBD_memcpy   memcpy

// ─── Device instance ID (passed to USBD_Init) ────────────────────────────────
#define DEVICE_FS  0U

// ─── Serial number string descriptor size (2 header + 12 chars × 2 bytes) ───
#define USB_SIZ_STRING_SERIAL  0x1AU

// ─── PCD handle — defined in usbd_conf.c ─────────────────────────────────────
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;
