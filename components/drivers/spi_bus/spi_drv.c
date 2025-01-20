/**
 * ESP-Drone Firmware
 *
 * Copyright 2019-2020  Espressif Systems (Shanghai)
 * Copyright (c) 2014, Bitcraze AB, All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 *
 * spi_drv.c - spi driver implementation
 *
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "stm32_legacy.h"
#include "spi_drv.h"
#include "config.h"
#define DEBUG_MODULE "SPIDRV"
#include "debug_cf.h"

// Definitions of sensors SPI bus speed
#define SPI_DEFAULT_SENSORS_CLOCK_SPEED             1000000

static bool isinit_spiPort[3] = {0, 0, 0};

// TODO : Add option to have more devices on the same SPI bus (usually only one MPU)

#ifdef CONFIG_VSPI_ENABLE
    static const spiDef sensorVBusDef = {
        .spiPort            = SPI3_HOST, //VSPI
        .sclk_pin           = CONFIG_VSPI_PIN_CLK,
        .mosi_pin           = CONFIG_VSPI_PIN_MOSI,
        .miso_pin           = CONFIG_VSPI_PIN_MISO,
        .cs_pin             = CONFIG_VSPI_PIN_CS0,
        .speed              = SPI_DEFAULT_SENSORS_CLOCK_SPEED,
        .max_transfer_size  = 4096,
    };
    static spi_device_handle_t vspi;
    spiDrv sensorsVBus = {
        .def                = &sensorVBusDef,
    };
#endif

#ifdef CONFIG_HSPI_ENABLE
    static const spiDef sensorHBusDef = {
        .spiPort            = HSPI,
        .sclk_pin           = CONFIG_HSPI_PIN_CLK,
        .mosi_pin           = CONFIG_HSPI_PIN_MOSI,
        .miso_pin           = CONFIG_HSPI_PIN_MISO,
        .cs_pin             = CONFIG_HSPI_PIN_CS0,
        .speed              = SPI_DEFAULT_SENSORS_CLOCK_SPEED,
        .max_transfer_size  = 4096,
    };
    static spi_device_handle_t hspi;
    spiDrv sensorsHBus = {
        .def                = &sensorHBusDef,
    };
#endif

// TODO : Define HSPI bus and initialize

void spidrvInitBus (spiDrv *spi)
{   
    if (isinit_spiPort[spi->def->spiPort]) {
        return;
    }

    spi_bus_config_t conf = {
        .mosi_io_num = spi->def->mosi_pin,
        .miso_io_num = spi->def->miso_pin,
        .sclk_io_num = spi->def->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = spi->def->max_transfer_size,
    };
    esp_err_t err = spi_bus_initialize(spi->def->spiPort, &conf, SPI_DMA_CH_AUTO);
    
    if (err != ESP_OK) {
        DEBUG_PRINTE("Failed to initialize SPI bus %d, error: %d", spi->def->spiPort, err);
        return;
    }   

    DEBUG_PRINTI("SPI bus %d initialized", spi->def->spiPort);
    spi->isBusFreeMutex = xSemaphoreCreateMutex();
    isinit_spiPort[spi->def->spiPort] = true;

    // Add a device to the bus
    spidrvAddDevice(spi);
}

void spidrvAddDevice(spiDrv *spi)
{
    if (spi == NULL || spi->def == NULL) {
        DEBUG_PRINTE("SPI bus or configuration not defined");
        return;
    }

    // if (spi->handle == NULL) {
    //     DEBUG_PRINTE("SPI device handle pointer is NULL");
    //     return;
    // }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = spi->def->speed,
        .mode = 0,  // SPI mode 0
        .spics_io_num = spi->def->cs_pin,
        .queue_size = 3,
    };

    esp_err_t ret = spi_bus_add_device(spi->def->spiPort, &devcfg, &vspi);
    if (ret != ESP_OK) {
        DEBUG_PRINTE("Failed to add device on SPI bus %d, error: %d", spi->def->spiPort, ret);
    } else {
        sensorsVBus.handle = vspi;
        DEBUG_PRINTI("Device added to SPI bus %d", spi->def->spiPort);
    }
}

void spidrvInit (spiDrv *spi){
    spidrvInitBus(spi);
}

void spidrvTryToRestartBus(spiDrv *spi)
{
    spidrvInitBus(spi);
}