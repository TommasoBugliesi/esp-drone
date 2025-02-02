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
#ifndef __MOTORS_BRUSHED_H__
#define __MOTORS_BRUSHED_H__

#include "motors.h"

/******** Defines ********/
#define MOTORS_PWM_BITS           LEDC_TIMER_8_BIT
#define MOTORS_PWM_PERIOD         ((1<<MOTORS_PWM_BITS) - 1)
#define MOTORS_TIM_BEEP_CLK_FREQ  4000000

/* 
*Compensate thrust depending on battery voltage so it will produce about the same
* amount of thrust independent of the battery voltage. Based on thrust measurement.
*/
//#define ENABLE_THRUST_BAT_COMPENSATED

#define MOT_PWM_CH1  4      // Motor M1 pwmchannel
#define MOT_PWM_CH2  5      // Motor M2 pwmchannel
#define MOT_PWM_CH3  6      // Motor M3 pwmchannel
#define MOT_PWM_CH4  7      // Motor M4 pwmchannel     

// Test defines
#define MOTORS_TEST_RATIO         (uint16_t)(0.2*(1<<16))
#define MOTORS_TEST_ON_TIME_MS    50
#define MOTORS_TEST_DELAY_TIME_MS 150

// Sound defines
#define C4    262
#define DES4  277
#define D4    294
#define ES4   311
#define E4    330
#define F4    349
#define GES4  370
#define G4    392
#define AS4   415
#define A4    440
#define B4    466
#define H4    493
#define C5    523
#define DES5  554
#define D5    587
#define ES5   622
#define E5    659
#define F5    698
#define GES5  740
#define G5    783
#define AS5   830
#define A5    880
#define B5    932
#define H5    987
#define C6    1046
#define DES6  1108
#define D6    1174
#define ES6   1244
#define E6    1318
#define F6    1396
#define GES6  1479
#define G6    1567
#define AS6   1661
#define A6    1760
#define B6    1864
#define H6    1975
#define C7    2093
#define DES7  2217
#define D7    2349
#define ES7   2489
#define E7    2637
#define F7    2793
#define GES7  2959
#define G7    3135
#define AS7   3322
#define A7    3520
#define H7    3729
#define B7    3951

// Sound duration defines
#define EIGHTS 125
#define QUAD 250
#define HALF 500
#define FULL 1000
#define STOP 0

/**
 * Test sound tones
 */
extern const uint16_t testsound[NBR_OF_MOTORS];


/*** Public interface ***/

/**
 * Initialisation. Will set all motors ratio to 0%
 */
void motorsBrushedInit(void);

/**
 * Test of the motor modules. The test will spin each motor very short in
 * the sequence M1 to M4.
 */
bool motorsBrushedTest(void);

/**
 * Update the motors driver
 */
void motorsBrushedApplyAll(uint16_t ithrust1, uint16_t ithrust2, uint16_t ithrust3, uint16_t ithrust4);


/**
 * Update a single motor driver
 */
void motorsBrushedApplyChannel(uint8_t channel, uint16_t ithrust);

#endif /* __MOTORS_H__ */

