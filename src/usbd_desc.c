// ─── usbd_desc.c ──────────────────────────────────────────────────────────────
// USB device descriptors — VID/PID, manufacturer, product, serial strings.
// VID 0x0483 + PID 0x5740 = ST Virtual COM Port (matches what every host
// already has a driver for; no custom INF needed on Windows 10+/Linux/macOS).

#include "usbd_desc.h"
#include "usbd_conf.h"
#include <string.h>

#define USBD_VID                    0x0483U
#define USBD_PID_FS                 0x5740U
#define USBD_LANGID_STRING          0x0409U   // English (US)
#define USBD_MANUFACTURER_STRING    "Astra Robotics"
#define USBD_PRODUCT_STRING_FS      "Astra Base Controller"
#define USBD_CONFIGURATION_STRING   "CDC Config"
#define USBD_INTERFACE_STRING       "CDC Interface"

// USB Standard Device Descriptor
static uint8_t USBD_FS_DeviceDesc[USB_LEN_DEV_DESC] = {
    0x12,               // bLength
    USB_DESC_TYPE_DEVICE, // bDescriptorType
    0x00, 0x02,         // bcdUSB: USB 2.0
    0x02,               // bDeviceClass: CDC
    0x02,               // bDeviceSubClass
    0x00,               // bDeviceProtocol
    USB_MAX_EP0_SIZE,   // bMaxPacketSize
    LOBYTE(USBD_VID), HIBYTE(USBD_VID),         // idVendor
    LOBYTE(USBD_PID_FS), HIBYTE(USBD_PID_FS),   // idProduct
    0x00, 0x02,         // bcdDevice
    USBD_IDX_MFC_STR,   // iManufacturer
    USBD_IDX_PRODUCT_STR, // iProduct
    USBD_IDX_SERIAL_STR,  // iSerialNumber
    USBD_MAX_NUM_CONFIGURATION  // bNumConfigurations
};

// Language ID descriptor
static uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] = {
    USB_LEN_LANGID_STR_DESC,
    USB_DESC_TYPE_STRING,
    LOBYTE(USBD_LANGID_STRING),
    HIBYTE(USBD_LANGID_STRING)
};

static uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ];

// Serial number from MCU unique ID
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
    for (uint8_t idx = 0; idx < len; idx++) {
        uint8_t nib = (value >> 28) & 0x0FU;
        pbuf[2 * idx]     = (nib < 10U) ? ('0' + nib) : ('A' + nib - 10U);
        pbuf[2 * idx + 1] = 0;
        value <<= 4;
    }
}

static void Get_SerialNum(void)
{
    uint32_t uid0 = *(uint32_t *)UID_BASE;
    uint32_t uid1 = *(uint32_t *)(UID_BASE + 4U);
    uint32_t uid2 = *(uint32_t *)(UID_BASE + 8U);
    uid0 += uid2;
    if (USBD_StrDesc[2] != 0) {
        IntToUnicode(uid0, &USBD_StrDesc[2], 8);
        IntToUnicode(uid1, &USBD_StrDesc[18], 4);
    }
}

static void USBD_UsrLog(const char *msg) { (void)msg; }

// ─── Descriptor callbacks ─────────────────────────────────────────────────────

static uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(USBD_FS_DeviceDesc);
    return USBD_FS_DeviceDesc;
}

static uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(USBD_LangIDDesc);
    return USBD_LangIDDesc;
}

static uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_FS, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = USB_SIZ_STRING_SERIAL;
    // serial number string header
    USBD_StrDesc[0] = *length;
    USBD_StrDesc[1] = USB_DESC_TYPE_STRING;
    Get_SerialNum();
    return USBD_StrDesc;
}

static uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

USBD_DescriptorsTypeDef FS_Desc = {
    USBD_FS_DeviceDescriptor,
    USBD_FS_LangIDStrDescriptor,
    USBD_FS_ManufacturerStrDescriptor,
    USBD_FS_ProductStrDescriptor,
    USBD_FS_SerialStrDescriptor,
    USBD_FS_ConfigStrDescriptor,
    USBD_FS_InterfaceStrDescriptor,
};
