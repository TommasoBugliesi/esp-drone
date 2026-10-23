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

#define SPI_MAX_TRANSFER_SIZE 4096
#define SPI_HOST_COUNT        3       // SPI1_HOST..SPI3_HOST

static bool isBusInit[SPI_HOST_COUNT];
static SemaphoreHandle_t busMutex[SPI_HOST_COUNT];

static const spiBusDef imuBus = {
    .host               = BOARD_IMU_SPI_HOST,
    .sclk_pin           = CONFIG_IMU_SPI_PIN_CLK,
    .mosi_pin           = CONFIG_IMU_SPI_PIN_MOSI,
    .miso_pin           = CONFIG_IMU_SPI_PIN_MISO,
    .max_transfer_size  = SPI_MAX_TRANSFER_SIZE,
};
static const spiDef imuDef = {
    .bus                = &imuBus,
    .cs_pin             = CONFIG_IMU_SPI_PIN_CS,
    .speed              = BOARD_IMU_SPI_HZ,
    .mode               = 0,
};
spiDrv imuSpiDev = { .def = &imuDef };

#ifdef CONFIG_TARGET_FLYINGFREE_V02
static const spiBusDef sensBus = {
    .host               = BOARD_SENS_SPI_HOST,
    .sclk_pin           = CONFIG_SENS_SPI_PIN_CLK,
    .mosi_pin           = CONFIG_SENS_SPI_PIN_MOSI,
    .miso_pin           = CONFIG_SENS_SPI_PIN_MISO,
    .max_transfer_size  = SPI_MAX_TRANSFER_SIZE,
};
static const spiDef magDef = {
    .bus                = &sensBus,
    .cs_pin             = CONFIG_MAG_SPI_PIN_CS,
    .speed              = BOARD_MAG_SPI_HZ,
    .mode               = 3,          // MMC5983MA: clock idles high, sampled on the rising edge
};
static const spiDef osdDef = {
    .bus                = &sensBus,
    .cs_pin             = CONFIG_OSD_SPI_PIN_CS,
    .speed              = BOARD_OSD_SPI_HZ,
    .mode               = 0,
};
spiDrv magSpiDev = { .def = &magDef };
spiDrv osdSpiDev = { .def = &osdDef };
#endif

static bool spidrvInitBus(const spiBusDef *bus)
{
    if (isBusInit[bus->host]) {
        return true;
    }

    spi_bus_config_t conf = {
        .mosi_io_num = bus->mosi_pin,
        .miso_io_num = bus->miso_pin,
        .sclk_io_num = bus->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = bus->max_transfer_size,
    };
    esp_err_t err = spi_bus_initialize(bus->host, &conf, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        DEBUG_PRINTE("Failed to initialize SPI bus %d, error: %d", bus->host, err);
        return false;
    }

    busMutex[bus->host] = xSemaphoreCreateMutex();
    isBusInit[bus->host] = true;
    DEBUG_PRINTI("SPI bus %d initialized", bus->host);
    return true;
}

static void spidrvAddDevice(spiDrv *spi)
{
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = spi->def->speed,
        .mode = spi->def->mode,
        .spics_io_num = spi->def->cs_pin,
        .queue_size = 3,
    };

    esp_err_t ret = spi_bus_add_device(spi->def->bus->host, &devcfg, &spi->handle);
    if (ret != ESP_OK) {
        DEBUG_PRINTE("Failed to add device (CS %d) on SPI bus %d, error: %d", spi->def->cs_pin,
                     spi->def->bus->host, ret);
        return;
    }
    spi->isBusFreeMutex = busMutex[spi->def->bus->host];
    DEBUG_PRINTI("Device (CS %d) added to SPI bus %d", spi->def->cs_pin, spi->def->bus->host);
}

void spidrvInit(spiDrv *spi)
{
    if (spi == NULL || spi->def == NULL) {
        DEBUG_PRINTE("SPI device not defined");
        return;
    }
    if (spi->handle != NULL) {
        return;
    }
    if (spidrvInitBus(spi->def->bus)) {
        spidrvAddDevice(spi);
    }
}
