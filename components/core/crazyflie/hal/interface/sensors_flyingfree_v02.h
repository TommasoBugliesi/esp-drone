/**
 * ESP-Drone Firmware
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * sensors_flyingfree_v02.h - Sensors of the FlyingFree V2 (ESP32-S3):
 * ICM-42688-P on SPI2, MMC5983MA on SPI3, DPS310 on I2C0.
 */

#ifndef __SENSORS_FLYINGFREE_V02_H__
#define __SENSORS_FLYINGFREE_V02_H__

#include "sensors.h"

void sensorsFF02Init(void);
bool sensorsFF02Test(void);
bool sensorsFF02AreCalibrated(void);
void sensorsFF02Acquire(sensorData_t *sensors, const uint32_t tick);
void sensorsFF02WaitDataReady(void);
bool sensorsFF02ReadGyro(Axis3f *gyro);
bool sensorsFF02ReadAcc(Axis3f *acc);
bool sensorsFF02ReadMag(Axis3f *mag);
bool sensorsFF02ReadBaro(baro_t *baro);
void sensorsFF02SetAccMode(accModes accMode);

#endif // __SENSORS_FLYINGFREE_V02_H__
