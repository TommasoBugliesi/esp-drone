/*
 * ICM-42688-P 6-axis IMU on SPI.
 *
 * The register setup is derived from Betaflight's accgyro_spi_icm426xx.c
 * (Cleanflight and Betaflight, author Dominic Clifton, GPL-3.0-or-later,
 * betaflight/betaflight commit 4b1aed7), adapted to the ESP-IDF SPI layer of
 * this project on 2026-10-23.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#ifndef ICM42688P_H_
#define ICM42688P_H_

#include <stdbool.h>
#include <stdint.h>

#define ICM42688P_WHO_AM_I_VALUE    0x47

// Full scale used by this driver: +-2000 deg/s and +-16 g
#define ICM42688P_GYRO_LSB_PER_DPS  16.4f
#define ICM42688P_ACC_LSB_PER_G     2048.0f
#define ICM42688P_TEMP_LSB_PER_DEGC 132.48f
#define ICM42688P_TEMP_OFFSET_DEGC  25.0f

// Raw data block: TEMP_DATA1 (0x1D) to GYRO_DATA_Z0 (0x2A), big endian
#define ICM42688P_DATA_LEN          14

typedef enum {
    ICM42688P_ODR_8KHZ = 3,
    ICM42688P_ODR_4KHZ = 4,
    ICM42688P_ODR_2KHZ = 5,
    ICM42688P_ODR_1KHZ = 6,
} icm42688pOdr_t;

typedef enum {
    ICM42688P_AAF_258HZ = 0,
    ICM42688P_AAF_536HZ,
    ICM42688P_AAF_997HZ,
    ICM42688P_AAF_1962HZ,
} icm42688pAaf_t;

typedef struct {
    int16_t temp;
    int16_t acc[3];
    int16_t gyro[3];
} icm42688pRaw_t;

/**
 * spiDev is an spiDrv * (void * keeps this header free of ESP-IDF types for the host tests).
 * Resets and configures the chip: low-noise mode for both sensors, the given
 * output data rate, the gyro anti-alias filter, and data ready on INT1
 * (push-pull, active high, 8 us pulse). Returns false if WHO_AM_I is wrong.
 */
bool icm42688pInit(void *spiDev, icm42688pOdr_t odr, icm42688pAaf_t gyroAaf);

// Reads temperature, accelerometer and gyro in one burst
bool icm42688pReadRaw(icm42688pRaw_t *out);

bool icm42688pTest(void);

// Unpacks the 14-byte data block (pure, used by the host tests)
void icm42688pDecode(const uint8_t buf[ICM42688P_DATA_LEN], icm42688pRaw_t *out);

#endif /* ICM42688P_H_ */
