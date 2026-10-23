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
 * led.h - LED functions header file
 */
#ifndef __LED_H__
#define __LED_H__

#include <stdbool.h>
#include "sdkconfig.h"

//Led polarity configuration constant
#define LED_POL_POS 0
#define LED_POL_NEG 1

// If colour not available skip. A role whose LED is missing moves to another LED.
#ifdef CONFIG_LED_PIN_BLUE_ENABLE
    #define LED_PRESENT_BLUE 1
    #define LED_GPIO_BLUE  CONFIG_LED_PIN_BLUE
#else
    #define LED_PRESENT_BLUE 0
    #define LED_GPIO_BLUE -1
#endif
#define LED_POL_BLUE   LED_POL_POS

#ifdef CONFIG_LED_PIN_RED_ENABLE
    #define LED_PRESENT_RED 1
    #define LED_GPIO_RED   CONFIG_LED_PIN_RED
#else
    #define LED_PRESENT_RED 0
    #define LED_GPIO_RED -1
#endif
#define LED_POL_RED    LED_POL_POS

#ifdef CONFIG_LED_PIN_GREEN_ENABLE
    #define LED_PRESENT_GREEN 1
    #define LED_GPIO_GREEN CONFIG_LED_PIN_GREEN
#else
    #define LED_PRESENT_GREEN 0
    #define LED_GPIO_GREEN -1
#endif
#define LED_POL_GREEN  LED_POL_POS

#if LED_PRESENT_RED
    #define CHG_LED          LED_RED
    #define LOWBAT_LED       LED_RED
    #define ERR_LED1         LED_RED
    #define ERR_LED2         LED_RED
#else
    #define CHG_LED          -1
    #define LOWBAT_LED       -1
    #define ERR_LED1         -1
    #define ERR_LED2         -1
#endif

#if LED_PRESENT_GREEN
    #define LINK_LED         LED_GREEN
#else
    #define LINK_LED         -1
#endif

#if LED_PRESENT_BLUE
    #define LINK_DOWN_LED    LED_BLUE
    #define SYS_LED          LED_BLUE
#else
    // FlyingFree V2 has red and green only
    #define LINK_DOWN_LED    LOWBAT_LED
    #define SYS_LED          LINK_LED
#endif

#define LED_NUM_MAX 3

typedef enum {LED_BLUE = 0, LED_RED = 1, LED_GREEN = 2} led_t;

void ledInit();
bool ledTest();

// Clear all configured LEDs
void ledClearAll(void);

// Set all configured LEDs
void ledSetAll(void);

// Procedures to set the status of the LEDs
void ledSet(led_t led, bool value);

void ledTask(void *param);

//Legacy functions
#define ledSetRed(VALUE) ledSet(LED_RED, VALUE)
#define ledSetGreen(VALUE) ledSet(LED_GREEN, VALUE)

#endif
