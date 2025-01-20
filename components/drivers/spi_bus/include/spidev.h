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
 *
 * spidev.h - Functions to write to spi devices
 */

#ifndef SPIDEV_H
#define SPIDEV_H

#include <stdint.h>
#include <stdbool.h>
#include "spi_drv.h"

typedef spiDrv    SPI_Dev;
#define VSPI_DEV  &sensorsVBus
#define HSPI_DEV  &sensorsHBus

// Initializes the SPI driver
int spidevInit(spiDrv *dev);

// Transfers data over SPI (both send and receive)
bool spidevTransfer(spiDrv *dev, uint8_t *txData, uint8_t *rxData, size_t length);

// Reads data from a specific register over SPI
bool spidevRead(spiDrv *dev, uint8_t regAddr, uint8_t *data, size_t length);

// Writes data to a specific register over SPI
bool spidevWrite(spiDrv *dev, uint8_t regAddr, const uint8_t *data, size_t length);

// Reads a single byte from a specific register
bool spidevReadByte(spiDrv *dev, uint8_t regAddr, uint8_t *data);

// Writes a single byte to a specific register
bool spidevWriteByte(spiDrv *dev, uint8_t regAddr, uint8_t data);

// Reads specific bits from a register
bool spidevReadBits(spiDrv *dev, uint8_t regAddr, uint8_t bitStart, uint8_t length, uint8_t *data);

// Writes specific bits to a register
bool spidevWriteBits(spiDrv *dev, uint8_t regAddr, uint8_t bitStart, uint8_t length, uint8_t data);

#endif // SPIDEV_H