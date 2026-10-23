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
 * Platform functionality for the different hardware
 */



#include <string.h>

#include "platform.h"
#define DEBUG_MODULE "PLATFORM"
#include "debug_cf.h"

#include "board.h"

// Only the board the firmware is built for: its sensor driver is the only one compiled in
static platformConfig_t configs[] = {
#ifdef CONFIG_TARGET_FLYINGFREE_V01
    {
        .deviceType = "FF01",
        .deviceTypeName = "FlyingFree V1.0 ",
        .sensorImplementation = SensorImplementation_flyingfree_v01,
        .physicalLayoutAntennasAreClose = false,
        .motorsType = MotorsTypeBrushless,
    },
#endif
#ifdef CONFIG_TARGET_FLYINGFREE_V02
    {
        .deviceType = "FF02",
        .deviceTypeName = "FlyingFree V2",
        .sensorImplementation = SensorImplementation_flyingfree_v02,
        .physicalLayoutAntennasAreClose = false,
        .motorsType = MotorsTypeBrushless,
    },
#endif
};

const platformConfig_t *platformGetListOfConfigurations(int *nrOfConfigs)
{
    *nrOfConfigs = sizeof(configs) / sizeof(platformConfig_t);
    return configs;
}

bool platformInitHardware()
{
    //TODO:
    return true;
}

// Config functions ------------------------

const char *platformConfigGetPlatformName()
{
    return BOARD_DEVICE_TYPE;
}
