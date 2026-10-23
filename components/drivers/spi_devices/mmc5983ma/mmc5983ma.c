/*
 * MEMSIC MMC5983MA 3-axis magnetometer on SPI (mode 3, up to 10 MHz).
 *
 * The SET/RESET offset measurement follows ArduPilot's AP_Compass_MMC5xx3.cpp
 * (GPL-3.0-or-later); the continuous mode and the 18-bit data layout follow the
 * MMC5983MA datasheet Rev A. Written for this project on 2026-10-23.
 *
 * SPI framing (datasheet "SPI Read and Write protocol"): first bit R/W (1 = read),
 * second bit don't care, then the 6-bit address. So a read is 0x80 | reg, which is
 * what spidevRead sends; multi-byte reads auto-increment.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#include "mmc5983ma.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spidev.h"
#define DEBUG_MODULE "MMC5983MA"
#include "debug_cf.h"

#define REG_XOUT0           0x00
#define REG_STATUS          0x08
#define STATUS_MEAS_M_DONE  (1 << 0)
#define REG_CONTROL0        0x09
#define CTRL0_TM_M          (1 << 0)
#define CTRL0_SET           (1 << 3)
#define CTRL0_RESET         (1 << 4)
#define CTRL0_AUTO_SR_EN    (1 << 5)
#define REG_CONTROL1        0x0A
#define CTRL1_BW_100HZ      0x00
#define CTRL1_BW_800HZ      0x03
#define CTRL1_SW_RST        (1 << 7)
#define REG_CONTROL2        0x0B
#define CTRL2_CM_100HZ      0x05
#define CTRL2_CM_1000HZ     0x07
#define CTRL2_CMM_EN        (1 << 3)
#define CTRL2_PRD_SET_100   (3 << 4)   // automatic SET every 100 measurements
#define CTRL2_EN_PRD_SET    (1 << 7)
#define REG_PRODUCT_ID      0x2F

static spiDrv *dev;
static bool isInit;
static float offset[3];

void mmc5983maDecode(const uint8_t buf[MMC5983MA_DATA_LEN], int32_t counts[3])
{
    // XOUT0/1, YOUT0/1, ZOUT0/1 hold bits 17..2, XYZOUT2 holds bits 1..0 of X (7:6), Y (5:4), Z (3:2)
    for (int i = 0; i < 3; i++) {
        const uint32_t high = ((uint32_t)buf[2 * i] << 10) | ((uint32_t)buf[2 * i + 1] << 2);
        const uint32_t low = (buf[6] >> (6 - 2 * i)) & 0x03;
        counts[i] = (int32_t)(high | low) - MMC5983MA_NULL_FIELD;
    }
}

void mmc5983maOffsetFromSetReset(const int32_t set[3], const int32_t reset[3], float offsetCounts[3])
{
    // After SET the output is offset + H, after RESET offset - H
    for (int i = 0; i < 3; i++) {
        offsetCounts[i] = 0.5f * (float)(set[i] + reset[i]);
    }
}

static bool writeReg(uint8_t reg, uint8_t value)
{
    return spidevWriteByte(dev, reg, value);
}

static bool measureOnce(int32_t counts[3])
{
    uint8_t status = 0;
    uint8_t buf[MMC5983MA_DATA_LEN];

    if (!writeReg(REG_CONTROL0, CTRL0_TM_M)) {
        return false;
    }
    // 8 ms at BW 100 Hz
    for (int i = 0; i < 20 && !(status & STATUS_MEAS_M_DONE); i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
        spidevReadByte(dev, REG_STATUS, &status);
    }
    if (!(status & STATUS_MEAS_M_DONE) || !spidevRead(dev, REG_XOUT0, buf, sizeof(buf))) {
        return false;
    }
    writeReg(REG_STATUS, STATUS_MEAS_M_DONE);
    mmc5983maDecode(buf, counts);
    return true;
}

bool mmc5983maInit(void *spiDev, mmc5983maRate_t rate)
{
    dev = (spiDrv *)spiDev;
    spidevInit(dev);

    // ArduPilot reports that the first product ID reads on SPI can fail
    uint8_t id = 0;
    for (int attempt = 0; attempt < 10 && id != MMC5983MA_PRODUCT_ID; attempt++) {
        spidevReadByte(dev, REG_PRODUCT_ID, &id);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (id != MMC5983MA_PRODUCT_ID) {
        DEBUG_PRINTE("product ID 0x%02X, expected 0x%02X", id, MMC5983MA_PRODUCT_ID);
        return false;
    }

    writeReg(REG_CONTROL1, CTRL1_SW_RST);
    vTaskDelay(pdMS_TO_TICKS(15));    // 10 ms power-on time
    writeReg(REG_CONTROL1, CTRL1_BW_100HZ);

    // Bridge offset: one measurement after SET, one after RESET
    int32_t setCounts[3], resetCounts[3];
    writeReg(REG_CONTROL0, CTRL0_SET);
    vTaskDelay(pdMS_TO_TICKS(1));
    bool ok = measureOnce(setCounts);
    writeReg(REG_CONTROL0, CTRL0_RESET);
    vTaskDelay(pdMS_TO_TICKS(1));
    ok = ok && measureOnce(resetCounts);
    if (!ok) {
        DEBUG_PRINTE("offset measurement failed");
        return false;
    }
    mmc5983maOffsetFromSetReset(setCounts, resetCounts, offset);
    // Continuous mode measures in the SET polarity. To check on the first board: the
    // datasheet does not say whether Auto_SR_en also removes the offset. If the field
    // at rest is off by the offset printed below, drop the subtraction in mmc5983maRead().
    writeReg(REG_CONTROL0, CTRL0_SET);
    vTaskDelay(pdMS_TO_TICKS(1));

    const bool fast = (rate == MMC5983MA_RATE_1000HZ);
    writeReg(REG_CONTROL1, fast ? CTRL1_BW_800HZ : CTRL1_BW_100HZ);
    writeReg(REG_CONTROL0, CTRL0_AUTO_SR_EN);
    writeReg(REG_CONTROL2, CTRL2_EN_PRD_SET | CTRL2_PRD_SET_100 | CTRL2_CMM_EN |
                           (fast ? CTRL2_CM_1000HZ : CTRL2_CM_100HZ));

    isInit = true;
    DEBUG_PRINTI("MMC5983MA ready, %s, offset %d %d %d counts", fast ? "1 kHz" : "100 Hz",
                 (int)offset[0], (int)offset[1], (int)offset[2]);
    return true;
}

bool mmc5983maRead(float gauss[3])
{
    uint8_t status = 0;
    uint8_t buf[MMC5983MA_DATA_LEN];
    int32_t counts[3];

    if (!isInit || !spidevReadByte(dev, REG_STATUS, &status) || !(status & STATUS_MEAS_M_DONE)) {
        return false;
    }
    if (!spidevRead(dev, REG_XOUT0, buf, sizeof(buf))) {
        return false;
    }
    writeReg(REG_STATUS, STATUS_MEAS_M_DONE);

    mmc5983maDecode(buf, counts);
    for (int i = 0; i < 3; i++) {
        gauss[i] = ((float)counts[i] - offset[i]) / MMC5983MA_COUNTS_PER_GAUSS;
    }
    return true;
}

bool mmc5983maTest(void)
{
    uint8_t id = 0;
    return isInit && spidevReadByte(dev, REG_PRODUCT_ID, &id) && id == MMC5983MA_PRODUCT_ID;
}
