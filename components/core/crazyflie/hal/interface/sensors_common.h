/**
 * ESP-Drone Firmware
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * sensors_common.h - Sensor processing shared by the boards: gyro bias,
 * accelerometer scale, alignment and filtering. No hardware access.
 *
 * Taken from sensors_flyingfree_v01.c (Bitcraze / Espressif / FlyingFree)
 * and made reusable. FlyingFree V2 uses it; V1.0 still has its own copy.
 */

#ifndef SENSORS_COMMON_H_
#define SENSORS_COMMON_H_

#include <stdbool.h>
#include <stdint.h>

#include "imu_types.h"
#include "filter.h"

#define SENSORS_NBR_OF_BIAS_SAMPLES 1024
#define SENSORS_ACC_SCALE_SAMPLES   200

typedef struct {
    Axis3f bias;
    Axis3f variance;
    Axis3f mean;
    float varianceThreshold;          // raw units squared, per axis
    uint32_t minIntervalTicks;        // between two bias updates
    uint32_t lastUpdateTick;
    bool isBiasValueFound;
    bool isBufferFilled;
    Axis3i16 *bufHead;
    Axis3i16 buffer[SENSORS_NBR_OF_BIAS_SAMPLES];
} sensorsBias_t;

typedef struct {
    float sum;
    uint32_t count;
    float scale;                      // measured magnitude of 1 g, starts at 1
    bool isFound;
} sensorsAccScale_t;

typedef struct {
    float cosPitch, sinPitch;
    float cosRoll, sinRoll;
} sensorsAlign_t;

void sensorsBiasInit(sensorsBias_t *bias, float varianceThreshold, uint32_t minIntervalTicks);

/**
 * Adds a raw gyro sample. When the last SENSORS_NBR_OF_BIAS_SAMPLES samples are
 * quiet enough the bias is taken from their mean. Returns true once a bias is found.
 * nowTicks is the current tick count.
 */
bool sensorsBiasUpdate(sensorsBias_t *bias, int16_t x, int16_t y, int16_t z, uint32_t nowTicks);

/** Variance and mean of the sample buffer (raw units). */
void sensorsBiasVarianceAndMean(const sensorsBias_t *bias, Axis3f *varOut, Axis3f *meanOut);

void sensorsAccScaleInit(sensorsAccScale_t *accScale);

/** Adds the magnitude of a steady sample in g. Returns true once the scale is known. */
bool sensorsAccScaleUpdate(sensorsAccScale_t *accScale, float magnitudeG);

void sensorsAlignInit(sensorsAlign_t *align, float pitchDeg, float rollDeg);

/** Rotates the accelerometer by the trim angles so that it reads gravity on Z at rest. */
void sensorsAlignToGravity(const sensorsAlign_t *align, const Axis3f *in, Axis3f *out);

/** out = m * in, for the sensor-to-body rotation of a board. */
void sensorsRotate(const float m[3][3], const Axis3f *in, Axis3f *out);

void sensorsLpfInit(lpf2pData lpf[3], float sampleHz, float cutoffHz);
void sensorsLpfApply(lpf2pData lpf[3], Axis3f *inOut);

#endif /* SENSORS_COMMON_H_ */
