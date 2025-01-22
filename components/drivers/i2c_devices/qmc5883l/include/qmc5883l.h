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

#ifndef QMC5883L_H_
#define QMC5883L_H_
#include <stdbool.h>
#include "i2cdev.h"

#define QMC5883L_REG_XOUT_L    0x00
#define QMC5883L_REG_XOUT_H    0x01
#define QMC5883L_REG_YOUT_L    0x02
#define QMC5883L_REG_YOUT_H    0x03
#define QMC5883L_REG_ZOUT_L    0x04
#define QMC5883L_REG_ZOUT_H    0x05
#define QMC5883L_REG_STATE     0x06
#define QMC5883L_REG_TOUT_L    0x07
#define QMC5883L_REG_TOUT_H    0x08
#define QMC5883L_REG_CTRL1     0x09
#define QMC5883L_REG_CTRL2     0x0a
#define QMC5883L_REG_FBR       0x0b
#define QMC5883L_REG_ID        0x0d
#define QMC5883L_CHIP_ID       0xff
#define QMC5883L_I2C_ADDR_DEF  0x0d

#define QMC5883L_MASK_MODE  0xfe
#define QMC5883L_MASK_ODR   0xf3

#define QMC5883L_STATUS_READY_BIT   0x00
#define QMC5883L_ST_DELAY_MS        250

#define QMC5883L_ODR_10     0x00   //!< 10Hz
#define QMC5883L_ODR_50     0x01   //!< 50Hz
#define QMC5883L_ODR_100    0x02   //!< 100Hz
#define QMC5883L_ODR_200    0x03   //!< 200Hz

#define QMC5883L_OSR_64     0x00   //!< 64 samples
#define QMC5883L_OSR_128    0x01   //!< 128 samples
#define QMC5883L_OSR_256    0x02   //!< 256 samples
#define QMC5883L_OSR_512    0x03   //!< 512 samples

#define QMC5883L_RNG_2      0x00   //!< -2G..+2G
#define QMC5883L_RNG_8      0x01   //!< -8G..+8G

#define QMC5883L_MODE_STANDBY  0x00   //!< Standby low power mode, no measurements
#define QMC5883L_MODE_CONTINUOUS 0x01   //!< Continuous measurements

/**
 * Raw measurement result
 */
struct qmc5883l_raw_data_t
{
    int16_t x;
    int16_t y;
    int16_t z;
};

/* Funtions start*/
/**
 * @brief Initialize device descriptor
 *
 * @param i2cPort I2C port number
 */
void qmc5883lInit(I2C_Dev *i2cPort);

/**
 * @brief Reset device
 */
void qmc5883lReset();

/** Get ID
 * @return ID byte (should be 0xff
 */
uint8_t qmc5883lGetID();

/**
 * @brief Get magnetic data state
 * @return True on success
 */
bool qmc5883lGetReadyStatus();

/**
 * @brief Read raw magnetic data
 *
 * @param[out] raw Raw magnetic data
 */
void qmc5883lGetHeading(struct qmc5883l_raw_data_t *raw);

/**
 * @brief Set device mode
 *
 * @param osr Oversampling
 * @param rng Field range
 * @param odr Output data rate
 * @param mode Mode
 */
void qmc5883lSetReg1(uint8_t osr, uint8_t rng, uint8_t odr, uint8_t mode);

/** Do a self test.
 * @return True if self test passed, false otherwise
 */
bool qmc5883lSelfTest();
/* Funtions end*/

#endif /* QMC5883L_H_ */