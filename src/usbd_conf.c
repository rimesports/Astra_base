// ─── usbd_conf.c ──────────────────────────────────────────────────────────────
// HAL-level bridge between the USB Device middleware and the STM32 PCD driver.
// All USBD_LL_* functions are called by the middleware; they forward to HAL_PCD_*.
// All HAL_PCD_*Callback functions are called by HAL; they forward to USBD_LL_*.

#include "usbd_conf.h"
#include "usbd_core.h"
#include "usbd_cdc.h"

PCD_HandleTypeDef hpcd_USB_OTG_FS;
static uint32_t usbd_cdc_mem[(sizeof(USBD_CDC_HandleTypeDef) + 3U) / 4U];

void *USBD_static_malloc(uint32_t size)
{
    if (size > sizeof(usbd_cdc_mem)) {
        return NULL;
    }
    return usbd_cdc_mem;
}

void USBD_static_free(void *p)
{
    (void)p;
}

// ─── HAL PCD MSP — GPIO + clock + NVIC ───────────────────────────────────────

void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if (hpcd->Instance == USB_OTG_FS) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        // PA11 = USB_DM, PA12 = USB_DP — AF10
        GPIO_InitStruct.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
        __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *hpcd)
{
    if (hpcd->Instance == USB_OTG_FS) {
        __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
        HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    }
}

// ─── USBD_LL_* — called by USB Device middleware ──────────────────────────────

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
    memset(&hpcd_USB_OTG_FS, 0, sizeof(hpcd_USB_OTG_FS));
    hpcd_USB_OTG_FS.Instance                 = USB_OTG_FS;
    hpcd_USB_OTG_FS.Init.dev_endpoints       = 4;
    hpcd_USB_OTG_FS.Init.Host_channels       = 0;
    hpcd_USB_OTG_FS.Init.speed               = PCD_SPEED_FULL;
    hpcd_USB_OTG_FS.Init.dma_enable          = DISABLE;
    hpcd_USB_OTG_FS.Init.ep0_mps             = EP_MPS_64;
    hpcd_USB_OTG_FS.Init.phy_itface          = PCD_PHY_EMBEDDED;
    hpcd_USB_OTG_FS.Init.Sof_enable          = DISABLE;
    hpcd_USB_OTG_FS.Init.low_power_enable    = DISABLE;
    hpcd_USB_OTG_FS.Init.lpm_enable          = DISABLE;
    hpcd_USB_OTG_FS.Init.battery_charging_enable = DISABLE;
    hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;  // no VBUS pin on Black Pill
    hpcd_USB_OTG_FS.Init.use_dedicated_ep1   = DISABLE;
    hpcd_USB_OTG_FS.Init.use_external_vbus   = DISABLE;
    pdev->pData = &hpcd_USB_OTG_FS;
    hpcd_USB_OTG_FS.pData = pdev;
    if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK) {
        return USBD_FAIL;
    }

    // FIFO sizes (words) — STM32F411 OTG FS has 320 words (0x140) total RAM.
    //   RX       = 0x80 = 128  (shared by all OUT endpoints + SETUP packets)
    //   EP0 TX   = 0x10 =  16  (EP0 max packet = 64 B = 16 words; exact fit)
    //   EP1 TX   = 0x40 =  64  (CDC bulk data IN, 64-byte packets)
    //   EP2 TX   = 0x10 =  16  (CDC command/notification IN, 8-byte packets)
    //   Total    = 0xE0 = 224  <= 320 — OK
    if (HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x80) != HAL_OK) {
        return USBD_FAIL;
    }
    if (HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x10) != HAL_OK) {
        return USBD_FAIL;
    }
    if (HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x40) != HAL_OK) {
        return USBD_FAIL;
    }
    if (HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 2, 0x10) != HAL_OK) {
        return USBD_FAIL;
    }

    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_DeInit(pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_Start(pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    HAL_PCD_Stop(pdev->pData);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                   uint8_t ep_type, uint16_t ep_mps)
{
    HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_Close(pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_Flush(pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_SetStall(pdev->pData, ep_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);
    return USBD_OK;
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef *hpcd = pdev->pData;
    if ((ep_addr & 0x80U) == 0x80U)
        return hpcd->IN_ep[ep_addr & 0x7FU].is_stall;
    return hpcd->OUT_ep[ep_addr & 0x7FU].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
    HAL_PCD_SetAddress(pdev->pData, dev_addr);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                     uint8_t *pbuf, uint32_t size)
{
    HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                           uint8_t *pbuf, uint32_t size)
{
    HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);
    return USBD_OK;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return HAL_PCD_EP_GetRxCount(pdev->pData, ep_addr);
}

void USBD_LL_Delay(uint32_t Delay)
{
    HAL_Delay(Delay);
}

// ─── HAL PCD callbacks → forward to USB Device middleware ────────────────────

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SetupStage(hpcd->pData, (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataOutStage(hpcd->pData, epnum, hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_DataInStage(hpcd->pData, epnum, hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SOF(hpcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_SetSpeed(hpcd->pData, USBD_SPEED_FULL);
    USBD_LL_Reset(hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Suspend(hpcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_Resume(hpcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete(hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete(hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_DevConnected(hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
    USBD_LL_DevDisconnected(hpcd->pData);
}
