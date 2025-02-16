
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

#include "spidev.h"
#include <string.h>
#include "debug_cf.h"

int spidevInit(spiDrv *dev) {
    spidrvInit(dev);
    return true;
}

bool spidevTransfer(spiDrv *dev, uint8_t *txData, uint8_t *rxData, size_t length) {
    if (xSemaphoreTake(dev->isBusFreeMutex, pdMS_TO_TICKS(5)) == pdFALSE) {
        return false;
    }

    spi_transaction_t transaction = {
        .length = length * 8, // Length in bits
        .tx_buffer = txData,
        .rx_buffer = rxData,
    };

    esp_err_t err = spi_device_transmit(dev->handle, &transaction);
    xSemaphoreGive(dev->isBusFreeMutex);

    if (err == ESP_OK) {
        return true;
    } else {
        DEBUG_PRINTE("SPI Transfer failed: error %d", err);
        return false;
    }
}

bool spidevRead(spiDrv *dev, uint8_t regAddr, uint8_t *data, size_t length) {
    uint8_t txBuffer[length + 1];
    uint8_t rxBuffer[length + 1];

    txBuffer[0] = regAddr | 0x80; // MSB set for read
    memset(&txBuffer[1], 0, length);

    if (!spidevTransfer(dev, txBuffer, rxBuffer, length + 1)) {
        return false;
    }

    memcpy(data, &rxBuffer[1], length); // Skip the first byte (dummy)
    return true;
}

bool spidevWrite(spiDrv *dev, uint8_t regAddr, const uint8_t *data, size_t length) {
    uint8_t txBuffer[length + 1];

    txBuffer[0] = regAddr & 0x7F; // MSB cleared for write
    memcpy(&txBuffer[1], data, length);

    return spidevTransfer(dev, txBuffer, NULL, length + 1);
}

bool spidevReadByte(spiDrv *dev, uint8_t regAddr, uint8_t *data) {
    return spidevRead(dev, regAddr, data, 1);
}

bool spidevWriteByte(spiDrv *dev, uint8_t regAddr, uint8_t data) {
    return spidevWrite(dev, regAddr, &data, 1);
}

bool spidevReadBits(spiDrv *dev, uint8_t regAddr, uint8_t bitStart, uint8_t length, uint8_t *data) {
    uint8_t byte;
    if (!spidevReadByte(dev, regAddr, &byte)) {
        return false;
    }

    uint8_t mask = ((1 << length) - 1) << (bitStart - length + 1);
    byte &= mask;
    byte >>= (bitStart - length + 1);
    *data = byte;
    return true;
}

bool spidevWriteBits(spiDrv *dev, uint8_t regAddr, uint8_t bitStart, uint8_t length, uint8_t data) {
    uint8_t byte;
    if (!spidevReadByte(dev, regAddr, &byte)) {
        return false;
    }

    uint8_t mask = ((1 << length) - 1) << (bitStart - length + 1);
    data <<= (bitStart - length + 1); // Shift data into position
    data &= mask;                     // Mask non-relevant bits
    byte &= ~mask;                    // Clear relevant bits in existing byte
    byte |= data;                     // Combine data with existing byte
    return spidevWriteByte(dev, regAddr, byte);
}