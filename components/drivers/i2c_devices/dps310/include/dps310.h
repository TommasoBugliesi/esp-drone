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

#ifndef DPS310_H_
#define DPS310_H_

#include <stdbool.h>
#include <stdint.h>

#define DPS310_I2C_ADDR_SDO_HIGH    0x77
#define DPS310_I2C_ADDR_SDO_LOW     0x76
#define DPS310_PRODUCT_ID           0x10
#define DPS310_COEF_LEN             18

typedef struct {
    int16_t c0;     // 12 bit
    int16_t c1;     // 12 bit
    int32_t c00;    // 20 bit
    int32_t c10;    // 20 bit
    int16_t c01;
    int16_t c11;
    int16_t c20;
    int16_t c21;
    int16_t c30;
} dps310Coef_t;

/**
 * i2cDev is an I2C_Dev *. Resets the chip, reads its calibration and starts
 * continuous pressure and temperature measurement at 32 Hz, 16x oversampling.
 */
bool dps310Init(void *i2cDev, uint8_t address);

/** Pressure in mbar, temperature in degC, altitude above sea level in m (standard atmosphere). */
bool dps310Read(float *pressureMbar, float *temperatureC, float *aslM);

bool dps310Test(void);

// Pure helpers, used by the host tests
void dps310ParseCoef(const uint8_t coef[DPS310_COEF_LEN], dps310Coef_t *out);
void dps310Compensate(const dps310Coef_t *c, int32_t rawP, int32_t rawT, float *pressurePa, float *temperatureC);
float dps310PressureToAltitude(float pressureMbar);
int32_t dps310TwosComplement(uint32_t raw, uint8_t bits);

#endif /* DPS310_H_ */
