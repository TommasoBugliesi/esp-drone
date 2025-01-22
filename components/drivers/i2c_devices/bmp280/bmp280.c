/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 sheinz <https://github.com/sheinz>
 * Copyright (c) 2018 Ruslan V. Uss <unclerus@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *  
 * Adapted for crayflie from Tommaso Bugliesi
 */
#define DEBUG_MODULE "BMP280"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bmp280.h"
#include "i2cdev.h"
#include "debug_cf.h"
#include "eprintf.h"

#include "math.h"

static uint8_t devAddr;
static I2C_Dev *I2Cx;
static bool isInit;

bool bmp280Init(I2C_Dev *i2cPort){
    if (isInit) {
        return true;
    }

    I2Cx = i2cPort;
    devAddr = BMP280_I2C_ADDRESS_0;

    bmp280Reset(); // reset the device to populate its internal PROM registers
    vTaskDelay(M2T(100));

    isInit = true;

    return true;
}

void bmp280Reset(){
    i2cdevWriteByte(I2Cx, devAddr, BMP280_REG_RESET, BMP280_RESET_VALUE);
}

