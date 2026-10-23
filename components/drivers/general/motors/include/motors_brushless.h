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
 * motors_brushed.h - Motor driver header file
 *
 */
#ifndef __MOTORS_BRUSHLESS_H__
#define __MOTORS_BRUSHLESS_H__

#include "motors.h"
#include "driver/rmt.h"
#include "board.h"

#define DSHOT_THROTTLE_MIN 48
#define DSHOT_THROTTLE_MAX 2047
#define MAX_UINT16 4095
#define HALF_MAX_UINT16 2048
#define MIN_UINT16 0

static rmt_item32_t _dshotCmd[17];
#define RMT_CMD_SIZE (sizeof(_dshotCmd) / sizeof(_dshotCmd[0])) // Prevent variable definition rmt_item32_t _dshotCmd[17]; 

// RMT transmit channels of the motors: 1-4 on the ESP32, 0-3 on the S3 (its channels 4-7 only receive)
#define MOT_RMT_CH1  (BOARD_MOTOR_RMT_CHANNEL_0 + 0)
#define MOT_RMT_CH2  (BOARD_MOTOR_RMT_CHANNEL_0 + 1)
#define MOT_RMT_CH3  (BOARD_MOTOR_RMT_CHANNEL_0 + 2)
#define MOT_RMT_CH4  (BOARD_MOTOR_RMT_CHANNEL_0 + 3)

#define MOTOR_TO_RMT_CHANNEL(motor_channel) (motor_channel == MOTOR_M1 ? MOT_RMT_CH1 : \
                                             motor_channel == MOTOR_M2 ? MOT_RMT_CH2 : \
                                             motor_channel == MOTOR_M3 ? MOT_RMT_CH3 : \
                                             motor_channel == MOTOR_M4 ? MOT_RMT_CH4 : \
                                             0)

#define MOTORS_TEST_RMT         49
#define MOTORS_TEST_ON_TIME_MS    50
#define MOTORS_TEST_DELAY_TIME_MS 150

#ifdef CONFIG_DSHOT_600
    #define DSHOT_FREQUENCY 600000UL
#endif
#ifdef CONFIG_DSHOT_300
    #define DSHOT_FREQUENCY 300000UL
#endif
#ifdef CONFIG_DSHOT_150
    #define DSHOT_FREQUENCY 150000UL
#endif

#define RMT_DIVIDER  3
#define WAIT_FOR_TX_DONE M2T(0)

enum DSHOT_CMD
{
    MOTOR_STOP,                            // Currently not implemented
    BEEP1,                                 // Wait at least length of beep (260ms) before next command
    BEEP2,                                 // Wait at least length of beep (260ms) before next command
    BEEP3,                                 // Wait at least length of beep (280ms) before next command
    BEEP4,                                 // Wait at least length of beep (280ms) before next command
    BEEP5,                                 // Wait at least length of beep (1020ms) before next command
    ESC_INFO,                              // Wait at least 12ms before next command
    SPIN_DIRECTION_1,                     // Need 6x, no wait required
    SPIN_DIRECTION_2,                     // Need 6x, no wait required
    MODE_3D_OFF,                          // Need 6x, no wait required
    MODE_3D_ON,                           // Need 6x, no wait required
    SETTINGS_REQUEST,                     // Currently not implemented
    SAVE_SETTINGS,                        // Need 6x, wait at least 35ms before next command
    SPIN_DIRECTION_NORMAL = 20,           // Need 6x, no wait required
    SPIN_DIRECTION_REVERSED,             // Need 6x, no wait required
    LED0_ON,                              // No wait required
    LED1_ON,                              // No wait required
    LED2_ON,                              // No wait required
    LED3_ON,                              // No wait required
    LED0_OFF,                             // No wait required
    LED1_OFF,                             // No wait required
    LED2_OFF,                             // No wait required
    LED3_OFF,                             // No wait required
};

/*** Public interface ***/

/**
 * Initialisation. Will set all motors to 0 throttle
 */
void motorsBrushlessInit(void);

/**
 * Test of the motor modules. The test will spin each motor very short in
 * the sequence M1 to M4.
 */
bool motorsBrushlessTest(void);

/**
 * Update the motors driver
 */
void motorsBrushlessApplyAll(uint16_t ithrust1, uint16_t ithrust2, uint16_t ithrust3, uint16_t ithrust4);

/**
 * Update a single motor driver
 */
void motorsBrushlessApplyChannel(uint8_t channel, uint16_t ithrust);

/**
 * Get a single motor driver
 */
int motorsBrushlessGetChannel(uint8_t id);

#endif /* __MOTORS_H__ */

