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
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * adc.c - Analog Digital Conversion
 *
 *
 */

#include "esp_idf_version.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/gpio.h"
#include "adc_esp32.h"
#include "config.h"
#include "pm_esplane.h"
#include "stm32_legacy.h"
#define DEBUG_MODULE "ADC"
#include "debug_cf.h"

static bool isInit;

static esp_adc_cal_characteristics_t *adc_chars;

// Battery voltage and ESC current. Only ADC1: ADC2 cannot be read while Wi-Fi runs.
#define ADC_MAX_PINS 2
static struct {
    int gpio;
    adc1_channel_t channel;
} adcPins[ADC_MAX_PINS];
static int adcPinCount;

static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = (adc_atten_t)3; // 11 dB (12 dB in newer IDF), as a value to avoid the deprecation warning
static const adc_unit_t unit = ADC_UNIT_1;
#define DEFAULT_VREF 1100 //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   30          //Multisampling

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        printf("Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        printf("Characterized using eFuse Vref\n");
    } else {
        printf("Characterized using Default Vref\n");
    }
}

/* ADC1 channel of a GPIO, configured on first use. -1 if the pin has no ADC1 channel. */
static int adcChannelOf(uint32_t pin)
{
    for (int i = 0; i < adcPinCount; i++) {
        if (adcPins[i].gpio == (int)pin) {
            return adcPins[i].channel;
        }
    }
    if (adcPinCount >= ADC_MAX_PINS) {
        DEBUG_PRINTE("no ADC slot left for GPIO%u", (unsigned)pin);
        return -1;
    }
    for (int ch = 0; ch < ADC1_CHANNEL_MAX; ch++) {
        gpio_num_t io;
        if (adc1_pad_get_io_num((adc1_channel_t)ch, &io) == ESP_OK && io == (gpio_num_t)pin) {
            adc1_config_channel_atten((adc1_channel_t)ch, atten);
            adcPins[adcPinCount].gpio = pin;
            adcPins[adcPinCount].channel = (adc1_channel_t)ch;
            adcPinCount++;
            return ch;
        }
    }
    DEBUG_PRINTE("GPIO%u is not an ADC1 pin", (unsigned)pin);
    return -1;
}

float analogReadVoltage(uint32_t pin)
{
    const int channel = adcChannelOf(pin);
    if (!isInit || channel < 0) {
        return 0.0f;
    }

    uint32_t adc_reading = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        adc_reading += adc1_get_raw((adc1_channel_t)channel);
    }
    adc_reading /= NO_OF_SAMPLES;
    //Convert adc_reading to voltage in mV
    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
    return voltage / 1000.0;
}

void adcInit(void)
{
    if (isInit) {
        return;
    }

    adc1_config_width(width);

    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    print_char_val_type(val_type);

    isInit = true;
}

bool adcTest(void)
{
    return isInit;
}
