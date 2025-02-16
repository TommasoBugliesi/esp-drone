/**
 *
 * ESP-Drone Firmware
 *
 * Copyright 2019-2020  Espressif Systems (Shanghai)
 * Copyright (C) 2011-2012 Bitcraze AB
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
 * Motors.h - Motor driver header file
 *
 */
#ifndef __MOTORS_H__
#define __MOTORS_H__

#include "stabilizer_types.h"
#include "sdkconfig.h"

#define NBR_OF_MOTORS 4 //Do not change 

#define MOTOR_M1  0
#define MOTOR_M2  1
#define MOTOR_M3  2
#define MOTOR_M4  3

#define MOTOR1_GPIO  CONFIG_MOTOR01_PIN   // M1 for ESP32FC
#define MOTOR2_GPIO  CONFIG_MOTOR02_PIN   // M2 for ESP32FC
#define MOTOR3_GPIO  CONFIG_MOTOR03_PIN   // M3 for ESP32FC
#define MOTOR4_GPIO  CONFIG_MOTOR04_PIN   // M4 for ESP32FC

typedef enum {
  MotorsTypeAny,
  MotorsTypeBrushed,
  MotorsTypeBrushless,
  MotorsType_COUNT,
} MotorsType;

void motorsInit(MotorsType motor);
bool motorsTest(void);
void motorsApplyAll(uint16_t ithrust1, uint16_t ithrust2, uint16_t ithrust3, uint16_t ithrust4);
void motorsApplyChannel(uint8_t channel, uint16_t ithrust);
int motorsGetChannel(uint8_t id);
MotorsType getMotorsType(void);
const char* motorsGetName();

#endif /* __MOTORS_H__ */

