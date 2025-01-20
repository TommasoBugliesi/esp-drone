/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * ESP-Drone Firmware
 *
 * Copyright 2019-2020  Espressif Systems (Shanghai)
 * Copyright (C) 2018 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * sensors.c - Abstraction layer for sensors on a platform. It acts as a
 * proxy to use the correct sensor based on device type.
 */

#define DEBUG_MODULE "SENSORS"

#include "sensors.h"
#include "platform.h"
#include "debug_cf.h"

// https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html
#define xstr(s) str(s)
#define str(s) #s

#ifdef CONFIG_TARGET_FLYINGFREE_V01
  #include "sensors_flyingfree_v01.h"
#endif

typedef struct {
  SensorImplementation_t implements;
  void (*init)(void);
  bool (*test)(void);
  bool (*areCalibrated)(void);
  bool (*manufacturingTest)(void);
  void (*acquire)(sensorData_t *sensors, const uint32_t tick);
  void (*waitDataReady)(void);
  bool (*readGyro)(Axis3f *gyro);
  bool (*readAcc)(Axis3f *acc);
  bool (*readMag)(Axis3f *mag);
  bool (*readBaro)(baro_t *baro);
  void (*setAccMode)(accModes accMode);
  void (*dataAvailableCallback)(void);
} sensorsImplementation_t;

// Suppressing compiler errors
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void nullFunction(void) {}
#pragma GCC diagnostic pop

static const sensorsImplementation_t sensorImplementations[SensorImplementation_COUNT] = {
#ifdef CONFIG_TARGET_FLYINGFREE_V01
  {
    .implements = SensorImplementation_flyingfree_v01,
    .init = sensorsFF01Init,
    .test = sensorsFF01Test,
    .areCalibrated = sensorsFF01AreCalibrated,
    .manufacturingTest = sensorsFF01ManufacturingTest,
    .acquire = sensorsFF01Acquire,
    .waitDataReady = sensorsFF01WaitDataReady,
    .readGyro = sensorsFF01ReadGyro,
    .readAcc = sensorsFF01ReadAcc,
    .readMag = sensorsFF01ReadMag,
    .readBaro = sensorsFF01ReadBaro,
    .setAccMode = sensorsFF01SetAccMode,
    .dataAvailableCallback = sensorsFF01DataAvailableCallback,
  },
#endif
};

static const sensorsImplementation_t* activeImplementation;
static bool isInit = false;
static const sensorsImplementation_t* findImplementation(SensorImplementation_t implementation);

void sensorsInit(void) {
  if (isInit) {
    return;
  }

#ifndef SENSORS_FORCE
  SensorImplementation_t sensorImplementation = platformConfigGetSensorImplementation();
#else
  SensorImplementation_t sensorImplementation = SENSORS_FORCE;
  DEBUG_PRINTD("Forcing sensors to " xstr(SENSORS_FORCE) "\n");
#endif

  activeImplementation = findImplementation(sensorImplementation);

  activeImplementation->init();

  isInit = true;
}

bool sensorsTest(void) {
  return activeImplementation->test();
}

bool sensorsAreCalibrated(void) {
  return activeImplementation->areCalibrated();
}

bool sensorsManufacturingTest(void){
  return activeImplementation->manufacturingTest;
}

void sensorsAcquire(sensorData_t *sensors, const uint32_t tick) {
  activeImplementation->acquire(sensors, tick);
}

void sensorsWaitDataReady(void) {
  activeImplementation->waitDataReady();
}

bool sensorsReadGyro(Axis3f *gyro) {
  return activeImplementation->readGyro(gyro);
}

bool sensorsReadAcc(Axis3f *acc) {
  return activeImplementation->readAcc(acc);
}

bool sensorsReadMag(Axis3f *mag) {
  return activeImplementation->readMag(mag);
}

bool sensorsReadBaro(baro_t *baro) {
  return activeImplementation->readBaro(baro);
}

void sensorsSetAccMode(accModes accMode) {
  activeImplementation->setAccMode(accMode);
}

void __attribute__((used)) EXTI14_Callback(void) {
  activeImplementation->dataAvailableCallback();
}

static const sensorsImplementation_t* findImplementation(SensorImplementation_t implementation) {
  const sensorsImplementation_t* result = 0;

  for (int i = 0; i < SensorImplementation_COUNT; i++) {
    if (sensorImplementations[i].implements == implementation) {
      result = &sensorImplementations[i];
      break;
    }
  }

  return result;
}
