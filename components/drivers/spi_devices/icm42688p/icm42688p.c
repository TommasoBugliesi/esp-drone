/*
 * ICM-42688-P 6-axis IMU on SPI.
 *
 * The register setup is derived from Betaflight's accgyro_spi_icm426xx.c
 * (Cleanflight and Betaflight, author Dominic Clifton, GPL-3.0-or-later,
 * betaflight/betaflight commit 4b1aed7), adapted to the ESP-IDF SPI layer of
 * this project on 2026-10-23: register access through spidev, FreeRTOS delays,
 * ICM-42688-P only, external clock (CLKIN) not used yet.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#include "icm42688p.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spidev.h"
#define DEBUG_MODULE "ICM42688P"
#include "debug_cf.h"

// User bank 0
#define REG_DEVICE_CONFIG           0x11
#define DEVICE_CONFIG_SOFT_RESET    0x01
#define REG_INT_CONFIG              0x14
#define INT1_PULSED_PP_ACTIVE_HIGH  ((0 << 2) | (1 << 1) | (1 << 0))
#define REG_TEMP_DATA1              0x1D
#define REG_INTF_CONFIG1            0x4D
#define INTF_CONFIG1_AFSR_MASK      0xC0
#define INTF_CONFIG1_AFSR_DISABLE   0x40   // avoids gyro output stalls (ArduPilot PR 25332)
#define REG_PWR_MGMT0               0x4E
#define PWR_MGMT0_OFF               0x00
#define PWR_MGMT0_GYRO_ACC_LN       ((3 << 2) | (3 << 0))
#define REG_GYRO_CONFIG0            0x4F
#define REG_ACCEL_CONFIG0           0x50
#define FS_SEL_MAX                  (0 << 5)   // +-2000 deg/s, +-16 g
#define REG_GYRO_ACCEL_CONFIG0      0x52
#define UI_FILT_LOW_LATENCY         ((15 << 4) | (15 << 0))
#define REG_INT_CONFIG0             0x63
#define DRDY_INT_CLEAR_ON_SBR       0x00
#define REG_INT_CONFIG1             0x64
#define INT_ASYNC_RESET_BIT         (1 << 4)
#define INT_TDEASSERT_DISABLED      (1 << 5)
#define INT_TPULSE_8US              (1 << 6)
#define REG_INT_SOURCE0             0x65
#define UI_DRDY_INT1_EN             (1 << 3)
#define REG_WHO_AM_I                0x75
#define REG_BANK_SEL                0x76

// User bank 1 and 2: anti-alias filter
#define REG_GYRO_CONFIG_STATIC3     0x0C
#define REG_GYRO_CONFIG_STATIC4     0x0D
#define REG_GYRO_CONFIG_STATIC5     0x0E
#define REG_ACCEL_CONFIG_STATIC2    0x03
#define REG_ACCEL_CONFIG_STATIC3    0x04
#define REG_ACCEL_CONFIG_STATIC4    0x05

typedef struct {
    uint8_t delt;
    uint16_t deltSqr;
    uint8_t bitshift;
} aafConfig_t;

// Datasheet section 5.3, values from Betaflight
static const aafConfig_t aafLut[] = {
    [ICM42688P_AAF_258HZ]  = {  6,   36, 10 },
    [ICM42688P_AAF_536HZ]  = { 12,  144,  8 },
    [ICM42688P_AAF_997HZ]  = { 21,  440,  6 },
    [ICM42688P_AAF_1962HZ] = { 37, 1376,  4 },
};

static spiDrv *dev;
static bool isInit;

static bool writeReg(uint8_t reg, uint8_t value)
{
    return spidevWriteByte(dev, reg, value);
}

static uint8_t readReg(uint8_t reg)
{
    uint8_t value = 0;
    spidevReadByte(dev, reg, &value);
    return value;
}

static void setBank(uint8_t bank)
{
    writeReg(REG_BANK_SEL, bank & 7);
}

void icm42688pDecode(const uint8_t buf[ICM42688P_DATA_LEN], icm42688pRaw_t *out)
{
    out->temp = (int16_t)((buf[0] << 8) | buf[1]);
    for (int i = 0; i < 3; i++) {
        out->acc[i] = (int16_t)((buf[2 + 2 * i] << 8) | buf[3 + 2 * i]);
        out->gyro[i] = (int16_t)((buf[8 + 2 * i] << 8) | buf[9 + 2 * i]);
    }
}

bool icm42688pInit(void *spiDev, icm42688pOdr_t odr, icm42688pAaf_t gyroAaf)
{
    dev = (spiDrv *)spiDev;
    spidevInit(dev);

    setBank(0);
    writeReg(REG_DEVICE_CONFIG, DEVICE_CONFIG_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(2));
    writeReg(REG_PWR_MGMT0, PWR_MGMT0_OFF);

    uint8_t whoAmI = 0;
    for (int attempt = 0; attempt < 20 && whoAmI != ICM42688P_WHO_AM_I_VALUE; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(1));
        whoAmI = readReg(REG_WHO_AM_I);
    }
    if (whoAmI != ICM42688P_WHO_AM_I_VALUE) {
        DEBUG_PRINTE("WHO_AM_I 0x%02X, expected 0x%02X", whoAmI, ICM42688P_WHO_AM_I_VALUE);
        return false;
    }

    // Sensors off while configuring (datasheet section 12.9)
    writeReg(REG_PWR_MGMT0, PWR_MGMT0_OFF);

    const aafConfig_t gyroAafCfg = aafLut[gyroAaf];
    setBank(1);
    writeReg(REG_GYRO_CONFIG_STATIC3, gyroAafCfg.delt);
    writeReg(REG_GYRO_CONFIG_STATIC4, gyroAafCfg.deltSqr & 0xFF);
    writeReg(REG_GYRO_CONFIG_STATIC5, (gyroAafCfg.deltSqr >> 8) | (gyroAafCfg.bitshift << 4));

    // Accelerometer anti-alias filter at 258 Hz
    const aafConfig_t accAafCfg = aafLut[ICM42688P_AAF_258HZ];
    setBank(2);
    writeReg(REG_ACCEL_CONFIG_STATIC2, accAafCfg.delt << 1);
    writeReg(REG_ACCEL_CONFIG_STATIC3, accAafCfg.deltSqr & 0xFF);
    writeReg(REG_ACCEL_CONFIG_STATIC4, (accAafCfg.deltSqr >> 8) | (accAafCfg.bitshift << 4));

    setBank(0);
    writeReg(REG_GYRO_ACCEL_CONFIG0, UI_FILT_LOW_LATENCY);

    // Data ready on INT1
    writeReg(REG_INT_CONFIG, INT1_PULSED_PP_ACTIVE_HIGH);
    writeReg(REG_INT_CONFIG0, DRDY_INT_CLEAR_ON_SBR);
    writeReg(REG_INT_SOURCE0, UI_DRDY_INT1_EN);
    uint8_t intConfig1 = readReg(REG_INT_CONFIG1);
    // Datasheet: change INT_ASYNC_RESET from its default 1 to 0 for proper INT1 and INT2 operation
    intConfig1 &= ~INT_ASYNC_RESET_BIT;
    intConfig1 |= INT_TPULSE_8US | INT_TDEASSERT_DISABLED;
    writeReg(REG_INT_CONFIG1, intConfig1);

    uint8_t intfConfig1 = readReg(REG_INTF_CONFIG1);
    intfConfig1 = (intfConfig1 & ~INTF_CONFIG1_AFSR_MASK) | INTF_CONFIG1_AFSR_DISABLE;
    writeReg(REG_INTF_CONFIG1, intfConfig1);

    writeReg(REG_PWR_MGMT0, PWR_MGMT0_GYRO_ACC_LN);
    vTaskDelay(pdMS_TO_TICKS(1));

    writeReg(REG_GYRO_CONFIG0, FS_SEL_MAX | (odr & 0x0F));
    vTaskDelay(pdMS_TO_TICKS(15));
    writeReg(REG_ACCEL_CONFIG0, FS_SEL_MAX | (odr & 0x0F));
    vTaskDelay(pdMS_TO_TICKS(15));

    isInit = true;
    DEBUG_PRINTI("ICM-42688-P ready, ODR code %d, gyro AAF %d", odr, gyroAaf);
    return true;
}

bool icm42688pReadRaw(icm42688pRaw_t *out)
{
    uint8_t buf[ICM42688P_DATA_LEN];
    if (!isInit || !spidevRead(dev, REG_TEMP_DATA1, buf, sizeof(buf))) {
        return false;
    }
    icm42688pDecode(buf, out);
    return true;
}

bool icm42688pTest(void)
{
    return isInit && readReg(REG_WHO_AM_I) == ICM42688P_WHO_AM_I_VALUE;
}
