/**
 * ESP-Drone Firmware
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * board.h - Facts of the selected board that are not pins.
 *
 * Pins are Kconfig settings (main/Kconfig.projbuild). Everything else that
 * differs between the boards lives here, so drivers do not test the board
 * name themselves.
 */

#ifndef BOARD_H_
#define BOARD_H_

#include "sdkconfig.h"

#if defined(CONFIG_TARGET_FLYINGFREE_V01)

#ifndef CONFIG_IDF_TARGET_ESP32
#error "FlyingFree V1.0 needs an ESP32 target (idf.py set-target esp32)"
#endif

#define BOARD_DEVICE_TYPE           "FF01"
#define BOARD_NAME                  "FlyingFree V1.0"

// SPI hosts (spi_host_device_t values)
#define BOARD_IMU_SPI_HOST          SPI3_HOST
#define BOARD_IMU_SPI_HZ            1000000

// DShot: RMT transmit channels of motors 1-4
#define BOARD_MOTOR_RMT_CHANNEL_0   1

// Battery divider 15 k / (15 k + 82 k)
#define BOARD_VBAT_DIVIDER          (15.0f / (15.0f + 82.0f))

#elif defined(CONFIG_TARGET_FLYINGFREE_V02)

#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "FlyingFree V2 needs an ESP32-S3 target (idf.py set-target esp32s3)"
#endif

#define BOARD_DEVICE_TYPE           "FF02"
#define BOARD_NAME                  "FlyingFree V2"

// The IMU has SPI2 to itself, the magnetometer and the OSD share SPI3
#define BOARD_IMU_SPI_HOST          SPI2_HOST
#define BOARD_IMU_SPI_HZ            10000000
#define BOARD_SENS_SPI_HOST         SPI3_HOST
#define BOARD_MAG_SPI_HZ            8000000
#define BOARD_OSD_SPI_HZ            8000000

// On the S3 only RMT channels 0-3 can transmit
#define BOARD_MOTOR_RMT_CHANNEL_0   0

// Battery divider 10 k / (10 k + 56 k)
#define BOARD_VBAT_DIVIDER          (10.0f / (10.0f + 56.0f))

#else
#error "No board selected (Kconfig: ESPDrone Config -> Board)"
#endif

#endif /* BOARD_H_ */
