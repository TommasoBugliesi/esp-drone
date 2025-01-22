/*
 * Copyright (c) 2019 Ruslan V. Uss <unclerus@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of itscontributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Adapted to Crazyflie FW by Tommaso Bugliesi */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "eprintf.h"
#include "qmc5883l.h"
#include "i2cdev.h"
#define DEBUG_MODULE "QMC5883L"
#include "debug_cf.h"

/* Variables section start */
static bool isInit;
static uint8_t buffer[6];
static uint8_t devAddr;
static I2C_Dev *I2Cx;
/* Variables section end */

/* Function section start */
void qmc5883lInit(I2C_Dev *i2cPort)
{
    if (isInit) {
        return;
    }

    I2Cx = i2cPort;
    devAddr = QMC5883L_I2C_ADDR_DEF;

    // soft reset
    qmc5883lReset();
    vTaskDelay(M2T(QMC5883L_ST_DELAY_MS));

    isInit = true;
}

void qmc5883lReset()
{
    i2cdevWriteByte(I2Cx, devAddr, QMC5883L_REG_CTRL2, 0x80);
}

uint8_t qmc5883lGetID()
{
    i2cdevReadByte(I2Cx, devAddr, QMC5883L_REG_ID, buffer);
    return buffer[0];
}

bool qmc5883lGetReadyStatus()
{
    i2cdevReadBit(I2Cx, devAddr, QMC5883L_REG_STATE, QMC5883L_STATUS_READY_BIT, buffer);
    return buffer[0];
}

void qmc5883lGetHeading(struct qmc5883l_raw_data_t *raw)
{
    i2cdevReadReg8(I2Cx, devAddr, QMC5883L_REG_XOUT_L, 6, buffer);

    raw->x = (((int16_t)buffer[0]) << 8) | buffer[1];
    raw->y = (((int16_t)buffer[4]) << 8) | buffer[5];
    raw->z = (((int16_t)buffer[2]) << 8) | buffer[3];
}

void qmc5883lSetReg1(uint8_t osr, uint8_t rng, uint8_t odr, uint8_t mode)
{
    uint8_t tmp = 0xff;

    tmp = (osr << 6) | (rng << 4) | (odr << 2) | mode; 
    i2cdevWriteByte(I2Cx, devAddr, QMC5883L_REG_CTRL1, tmp);
}

bool qmc5883lSelfTest()
{
    if (qmc5883lGetID() != QMC5883L_CHIP_ID) {
        return false;
    }

    // TODO improve self test with more robust implementation
    return true;
}
/* Functions section end */