/**
 * ESP-Drone Firmware
 *
 * Copyright 2019-2020  Espressif Systems (Shanghai)
 * Copyright (C) 2011-2012 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * sensors_common.c - Sensor processing shared by the boards. The algorithms
 * are those of sensors_flyingfree_v01.c, with their state passed in.
 */

#include "sensors_common.h"

#include <math.h>
#include <string.h>

void sensorsBiasInit(sensorsBias_t *bias, float varianceThreshold, uint32_t minIntervalTicks)
{
    memset(bias, 0, sizeof(*bias));
    bias->varianceThreshold = varianceThreshold;
    bias->minIntervalTicks = minIntervalTicks;
    bias->bufHead = bias->buffer;
}

void sensorsBiasVarianceAndMean(const sensorsBias_t *bias, Axis3f *varOut, Axis3f *meanOut)
{
    int64_t sum[3] = {0};
    int64_t sumSq[3] = {0};

    for (uint32_t i = 0; i < SENSORS_NBR_OF_BIAS_SAMPLES; i++) {
        sum[0] += bias->buffer[i].x;
        sum[1] += bias->buffer[i].y;
        sum[2] += bias->buffer[i].z;
        sumSq[0] += bias->buffer[i].x * bias->buffer[i].x;
        sumSq[1] += bias->buffer[i].y * bias->buffer[i].y;
        sumSq[2] += bias->buffer[i].z * bias->buffer[i].z;
    }

    for (int a = 0; a < 3; a++) {
        varOut->axis[a] = (float)(sumSq[a] - (sum[a] * sum[a]) / SENSORS_NBR_OF_BIAS_SAMPLES);
        meanOut->axis[a] = (float)sum[a] / SENSORS_NBR_OF_BIAS_SAMPLES;
    }
}

bool sensorsBiasUpdate(sensorsBias_t *bias, int16_t x, int16_t y, int16_t z, uint32_t nowTicks)
{
    // Circular buffer of the latest samples
    bias->bufHead->x = x;
    bias->bufHead->y = y;
    bias->bufHead->z = z;
    bias->bufHead++;
    if (bias->bufHead >= &bias->buffer[SENSORS_NBR_OF_BIAS_SAMPLES]) {
        bias->bufHead = bias->buffer;
        bias->isBufferFilled = true;
    }

    if (!bias->isBiasValueFound && bias->isBufferFilled) {
        sensorsBiasVarianceAndMean(bias, &bias->variance, &bias->mean);
        if (bias->variance.x < bias->varianceThreshold &&
            bias->variance.y < bias->varianceThreshold &&
            bias->variance.z < bias->varianceThreshold &&
            (bias->lastUpdateTick + bias->minIntervalTicks < nowTicks)) {
            bias->lastUpdateTick = nowTicks;
            bias->bias = bias->mean;
            bias->isBiasValueFound = true;
        }
    }

    return bias->isBiasValueFound;
}

void sensorsAccScaleInit(sensorsAccScale_t *accScale)
{
    memset(accScale, 0, sizeof(*accScale));
    accScale->scale = 1.0f;
}

bool sensorsAccScaleUpdate(sensorsAccScale_t *accScale, float magnitudeG)
{
    if (!accScale->isFound) {
        accScale->sum += magnitudeG;
        accScale->count++;
        if (accScale->count == SENSORS_ACC_SCALE_SAMPLES) {
            accScale->scale = accScale->sum / SENSORS_ACC_SCALE_SAMPLES;
            accScale->isFound = true;
        }
    }
    return accScale->isFound;
}

void sensorsAlignInit(sensorsAlign_t *align, float pitchDeg, float rollDeg)
{
    align->cosPitch = cosf(pitchDeg * (float)M_PI / 180);
    align->sinPitch = sinf(pitchDeg * (float)M_PI / 180);
    align->cosRoll = cosf(rollDeg * (float)M_PI / 180);
    align->sinRoll = sinf(rollDeg * (float)M_PI / 180);
}

void sensorsAlignToGravity(const sensorsAlign_t *align, const Axis3f *in, Axis3f *out)
{
    Axis3f rx;
    Axis3f ry;

    // Rotate around x-axis
    rx.x = in->x;
    rx.y = in->y * align->cosRoll - in->z * align->sinRoll;
    rx.z = in->y * align->sinRoll + in->z * align->cosRoll;

    // Rotate around y-axis
    ry.x = rx.x * align->cosPitch - rx.z * align->sinPitch;
    ry.y = rx.y;
    ry.z = -rx.x * align->sinPitch + rx.z * align->cosPitch;

    *out = ry;
}

void sensorsRotate(const float m[3][3], const Axis3f *in, Axis3f *out)
{
    Axis3f r;
    for (int i = 0; i < 3; i++) {
        r.axis[i] = m[i][0] * in->x + m[i][1] * in->y + m[i][2] * in->z;
    }
    *out = r;
}

void sensorsLpfInit(lpf2pData lpf[3], float sampleHz, float cutoffHz)
{
    for (int i = 0; i < 3; i++) {
        lpf2pInit(&lpf[i], sampleHz, cutoffHz);
    }
}

void sensorsLpfApply(lpf2pData lpf[3], Axis3f *inOut)
{
    for (int i = 0; i < 3; i++) {
        inOut->axis[i] = lpf2pApply(&lpf[i], inOut->axis[i]);
    }
}
