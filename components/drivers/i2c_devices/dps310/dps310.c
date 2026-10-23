/*
 * Infineon DPS310 barometer on I2C.
 *
 * Derived from barometer_dps310.c of Cleanflight, Betaflight and INAV
 * (Copyright INAVFLIGHT OU, MPL-2.0 or GPL-3.0-or-later, used here under the
 * GPL), betaflight/betaflight commit 4b1aed7, adapted to the i2cdev layer of
 * this project on 2026-10-23: DPS310 only, synchronous reads.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#include "dps310.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2cdev.h"
#define DEBUG_MODULE "DPS310"
#include "debug_cf.h"

#define REG_PSR_B2              0x00
#define REG_PRS_CFG             0x06
#define REG_TMP_CFG             0x07
#define REG_MEAS_CFG            0x08
#define REG_CFG_REG             0x09
#define REG_RESET               0x0C
#define REG_ID                  0x0D
#define REG_COEF                0x10
#define REG_COEF_SRCE           0x28

#define RESET_SOFT_RST          0x09
#define MEAS_CFG_COEF_RDY       (1 << 7)
#define MEAS_CFG_SENSOR_RDY     (1 << 6)
#define MEAS_CFG_CONT_PRS_TMP   0x07
#define PRS_CFG_32HZ_16X        (0x50 | 0x04)
#define TMP_CFG_32HZ_16X        (0x50 | 0x04)
#define COEF_SRCE_TMP_EXT       0x80
#define CFG_REG_P_SHIFT         0x04
#define CFG_REG_T_SHIFT         0x08

// Scale factor for 16x oversampling, datasheet table 9
#define KP_16X                  253952.0f
#define KT_16X                  253952.0f

static I2C_Dev *dev;
static uint8_t devAddr;
static bool isInit;
static dps310Coef_t coef;

int32_t dps310TwosComplement(uint32_t raw, uint8_t bits)
{
    if (raw & ((uint32_t)1 << (bits - 1))) {
        return (int32_t)raw - ((int32_t)1 << bits);
    }
    return (int32_t)raw;
}

void dps310ParseCoef(const uint8_t c[DPS310_COEF_LEN], dps310Coef_t *out)
{
    // Datasheet section 8.11
    out->c0  = dps310TwosComplement(((uint32_t)c[0] << 4) | ((c[1] >> 4) & 0x0F), 12);
    out->c1  = dps310TwosComplement((((uint32_t)c[1] & 0x0F) << 8) | c[2], 12);
    out->c00 = dps310TwosComplement(((uint32_t)c[3] << 12) | ((uint32_t)c[4] << 4) | ((c[5] >> 4) & 0x0F), 20);
    out->c10 = dps310TwosComplement((((uint32_t)c[5] & 0x0F) << 16) | ((uint32_t)c[6] << 8) | c[7], 20);
    out->c01 = dps310TwosComplement(((uint32_t)c[8] << 8) | c[9], 16);
    out->c11 = dps310TwosComplement(((uint32_t)c[10] << 8) | c[11], 16);
    out->c20 = dps310TwosComplement(((uint32_t)c[12] << 8) | c[13], 16);
    out->c21 = dps310TwosComplement(((uint32_t)c[14] << 8) | c[15], 16);
    out->c30 = dps310TwosComplement(((uint32_t)c[16] << 8) | c[17], 16);
}

void dps310Compensate(const dps310Coef_t *c, int32_t rawP, int32_t rawT, float *pressurePa, float *temperatureC)
{
    // Datasheet sections 4.9.1 and 4.9.2
    const float p = rawP / KP_16X;
    const float t = rawT / KT_16X;
    *pressurePa = c->c00 + p * (c->c10 + p * (c->c20 + p * c->c30)) + t * c->c01 + t * p * (c->c11 + p * c->c21);
    *temperatureC = c->c0 * 0.5f + c->c1 * t;
}

float dps310PressureToAltitude(float pressureMbar)
{
    // International standard atmosphere, sea level 1013.25 mbar
    return 44330.0f * (1.0f - powf(pressureMbar / 1013.25f, 0.190295f));
}

static uint8_t readReg(uint8_t reg)
{
    uint8_t value = 0;
    i2cdevReadByte(dev, devAddr, reg, &value);
    return value;
}

bool dps310Init(void *i2cDev, uint8_t address)
{
    dev = (I2C_Dev *)i2cDev;
    devAddr = address;

    uint8_t id = 0;
    for (int attempt = 0; attempt < 5 && id != DPS310_PRODUCT_ID; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(20));
        id = readReg(REG_ID);
    }
    if (id != DPS310_PRODUCT_ID) {
        DEBUG_PRINTE("ID 0x%02X, expected 0x%02X", id, DPS310_PRODUCT_ID);
        return false;
    }

    i2cdevWriteByte(dev, devAddr, REG_RESET, RESET_SOFT_RST);
    vTaskDelay(pdMS_TO_TICKS(40));

    const uint8_t status = readReg(REG_MEAS_CFG);
    if ((status & (MEAS_CFG_COEF_RDY | MEAS_CFG_SENSOR_RDY)) != (MEAS_CFG_COEF_RDY | MEAS_CFG_SENSOR_RDY)) {
        DEBUG_PRINTE("not ready after reset (MEAS_CFG 0x%02X)", status);
        return false;
    }

    // In two parts: the chip fails a single long read over I2C (Betaflight note)
    uint8_t raw[DPS310_COEF_LEN];
    if (!i2cdevReadReg8(dev, devAddr, REG_COEF, 9, raw) ||
        !i2cdevReadReg8(dev, devAddr, REG_COEF + 9, DPS310_COEF_LEN - 9, raw + 9)) {
        return false;
    }
    dps310ParseCoef(raw, &coef);

    const uint8_t tempSource = readReg(REG_COEF_SRCE) & COEF_SRCE_TMP_EXT;
    i2cdevWriteByte(dev, devAddr, REG_PRS_CFG, PRS_CFG_32HZ_16X);
    i2cdevWriteByte(dev, devAddr, REG_TMP_CFG, TMP_CFG_32HZ_16X | tempSource);
    // Result shift is required above 8x oversampling
    i2cdevWriteByte(dev, devAddr, REG_CFG_REG, CFG_REG_P_SHIFT | CFG_REG_T_SHIFT);
    i2cdevWriteByte(dev, devAddr, REG_MEAS_CFG, MEAS_CFG_CONT_PRS_TMP);

    isInit = true;
    DEBUG_PRINTI("DPS310 ready at 0x%02X", devAddr);
    return true;
}

bool dps310Read(float *pressureMbar, float *temperatureC, float *aslM)
{
    uint8_t buf[6];
    if (!isInit || !i2cdevReadReg8(dev, devAddr, REG_PSR_B2, sizeof(buf), buf)) {
        return false;
    }
    const int32_t rawP = dps310TwosComplement(((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2], 24);
    const int32_t rawT = dps310TwosComplement(((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5], 24);

    float pressurePa;
    dps310Compensate(&coef, rawP, rawT, &pressurePa, temperatureC);
    *pressureMbar = pressurePa / 100.0f;
    *aslM = dps310PressureToAltitude(*pressureMbar);
    return true;
}

bool dps310Test(void)
{
    return isInit && readReg(REG_ID) == DPS310_PRODUCT_ID;
}
