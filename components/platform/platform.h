/**
*
 * ESP-Drone Firmware
 *
 * Copyright 2019-2020  Espressif Systems (Shanghai)
 * Copyright (C) 2011-2018 Bitcraze AB
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
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_

#include <stdbool.h>

#include "motors.h"

#define PLATFORM_DEVICE_TYPE_STRING_MAX_LEN (32 + 1)
#define PLATFORM_DEVICE_TYPE_MAX_LEN (4 + 1)

typedef enum {
    SensorImplementation_flyingfree_v01,
    SensorImplementation_COUNT,
} SensorImplementation_t;

typedef struct {
    char deviceType[PLATFORM_DEVICE_TYPE_MAX_LEN];
    char deviceTypeName[20];
    SensorImplementation_t sensorImplementation;
    bool physicalLayoutAntennasAreClose;
    MotorsType motorsType;
} platformConfig_t;

/**
 * Initilizes all platform specific things.
 */
int platformInit(void);

void platformGetDeviceTypeString(char *deviceTypeString);
int platformParseDeviceTypeString(const char *deviceTypeString, char *deviceType);
int platformInitConfiguration(const platformConfig_t *configs, const int nrOfConfigs);

// Implemented in platform specific files
const platformConfig_t *platformGetListOfConfigurations(int *nrOfConfigs);
bool platformInitHardware();


void platformSetLowInterferenceRadioMode(void);

// Functions to read configuration
const char *platformConfigGetPlatformName();
const char *platformConfigGetDeviceType();
const char *platformConfigGetDeviceTypeName();
SensorImplementation_t platformConfigGetSensorImplementation();
bool platformConfigPhysicalLayoutAntennasAreClose();
MotorsType platformConfigGetMotorsType();

#endif /* PLATFORM_H_ */
