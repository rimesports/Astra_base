#include "config_store.h"
#include "astra_config.h"
#include "stm32f4xx_hal.h"
#include <string.h>

static uint32_t calc_checksum(const ConfigData *cfg)
{
    const uint32_t *p = (const uint32_t *)cfg;
    uint32_t n = (sizeof(ConfigData) - sizeof(uint32_t)) / 4;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) sum += p[i];
    return sum;
}

bool config_store_save(const ConfigData *cfg)
{
    ConfigData tmp = *cfg;
    tmp.magic    = CONFIG_MAGIC;
    tmp.version  = CONFIG_VERSION;
    tmp.checksum = calc_checksum(&tmp);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {};
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Sector       = CONFIG_FLASH_SECTOR;
    erase.NbSectors    = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    uint32_t sector_error = 0;
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    const uint32_t *src  = (const uint32_t *)&tmp;
    uint32_t        nw   = sizeof(ConfigData) / 4;
    uint32_t        addr = CONFIG_FLASH_ADDR;
    for (uint32_t i = 0; i < nw; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, src[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        addr += 4;
    }

    HAL_FLASH_Lock();
    return true;
}

bool config_store_load(ConfigData *cfg)
{
    const ConfigData *flash = (const ConfigData *)CONFIG_FLASH_ADDR;
    if (flash->magic != CONFIG_MAGIC || flash->version != CONFIG_VERSION) return false;
    *cfg = *flash;
    if (cfg->checksum != calc_checksum(cfg)) return false;
    return true;
}

void config_store_defaults(ConfigData *cfg)
{
    cfg->magic      = CONFIG_MAGIC;
    cfg->version    = CONFIG_VERSION;
    cfg->kp         = PID_KP;
    cfg->ki         = PID_KI;
    cfg->kd         = PID_KD;
    cfg->kf         = PID_KF;
    cfg->imu_ax_off = 0.0f;
    cfg->imu_ay_off = 0.0f;
    cfg->imu_az_off = 0.0f;
    cfg->imu_gx_off = 0.0f;
    cfg->imu_gy_off = 0.0f;
    cfg->imu_gz_off = 0.0f;
    cfg->checksum   = calc_checksum(cfg);
}
