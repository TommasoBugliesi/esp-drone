# Hardware

The FlyingFree V1.0 as the firmware sees it, and the decisions that are still open. The ESP32-S3 successor is
in [hardware/flyingfree-v2](../../hardware/flyingfree-v2/README.md).

> **No schematic of this board is in the repository.** [`hardware/`](../../hardware) holds the upstream
> ESP-Drone boards. Everything below comes from the Kconfig defaults and the code. Check pins against your
> own schematic before wiring anything new.

## Overview

| | |
|---|---|
| MCU | ESP32 (dual core), Wi-Fi and BLE |
| IMU | BMI160 on SPI3, polled at 1 kHz, interrupt pin not used yet (`IMU_INT1_PIN` = -1) |
| Magnetometer | QMC5883L on I2C0 (optional) |
| Barometer | BMP280 (optional, driver is a stub) |
| Range finder | VL53L0X on I2C0 (optional) |
| Motors | 4 brushless, DShot150 by default, RMT channels 1-4 |
| Battery | 3S or 4S, divider 15 k / 82 k to GPIO34 (`pm_esplane.h`) |
| Indicators | red, green, blue LED, one buzzer pin |
| Debug | SEGGER SystemView over JTAG, optional (`profiles/sysview.defaults`) |

## Pin map (defaults)

| Function | GPIO | Setting |
|---|---|---|
| I2C0 SDA / SCL | 21 / 22 | `I2C0_PIN_SDA`, `I2C0_PIN_SCL` |
| IMU SPI (SPI3) MISO / MOSI / CLK / CS | 19 / 23 / 18 / 5 | `IMU_SPI_PIN_*` |
| Motor 1 / 2 / 3 / 4 | 27 / 26 / 25 / 33 | `MOTOR01_PIN` to `MOTOR04_PIN` |
| LED blue / green / red | 16 / 4 / 17 | `LED_PIN_*` |
| Buzzer | 2 | `BUZ1_PIN_POS` |
| Battery voltage (ADC1 channel 6) | 34 | `VBAT_ADC_PIN` |

### What is left

| GPIO | Notes |
|---|---|
| 13, 14 | free, but they are JTAG TCK and TMS when `profiles/sysview.defaults` is used |
| 12, 15 | free, strapping pins (12 selects the flash voltage, keep it low at boot) and JTAG TDI / TDO |
| 0 | free, strapping pin (low at reset enters the bootloader), also the programmer's boot button |
| 32 | free |
| 35, 36, 39 | free, **input only** |
| 1, 3 | UART0 (console and flashing) |
| 6-11 | internal flash, not usable |

One input is still needed for the IMU data-ready interrupt (35, 36 or 39 are natural).

## Open decisions

### IMU and magnetometer

> **Decided for the ESP32-S3 board:** ICM-42688-P on SPI2 and an MMC5983MA magnetometer on SPI3. The
> BMI270 + BMM350 option below was dropped, see [ESP32-S3 board requirements](esp32-s3-board-requirements.md#imu-and-magnetometer-pairs-considered).
> It still describes the trade-off for the current ESP32 board.

Today the BMI160 gives accel and gyro and the QMC5883L sits on I2C0. A proposed alternative is a
**Bosch BMI270 with a BMM350** magnetometer attached to the IMU's auxiliary interface.

| Aspect | BMI160 + QMC5883L (now) | BMI270 + BMM350 on the aux bus |
|---|---|---|
| Magnetometer bus | main I2C0, read in the 1 kHz loop | the IMU's own aux I2C, the ESP32 reads it through the IMU |
| I2C0 afterwards | shared with the magnetometer | free for EEPROM, barometer, range finder |
| IMU start-up | register setup | an 8 KB configuration blob is uploaded at every boot (Bosch BMI2 API) |
| Driver | the existing BMI160 component | new BMI2 / BMM350 components, [Bosch SensorAPI](https://github.com/boschsensortec) (BSD-3) |
| Interrupt | not used | INT1/INT2 available for data-ready |
| Mag calibration | none in the firmware | the BMM350 compensates with factory OTP data, hard/soft iron still needed |

Bosch documents the BMI270 auxiliary interface for magnetometers and provides a BMM350 API. **Confirm the
exact combination and the aux wiring in both datasheets before the PCB is fixed.** The axes of the two chips
differ and the alignment (`IMU_X_TOFRAME` etc. in `sensors_flyingfree_v01.c`) has to be redone.

Software impact: replace the `bmi160` component with BMI2 and BMM350 components and rework
`sensors_flyingfree_v01.c`. The QMC5883L driver, `QMC5883L_ENABLE` and the magnetometer remap would go
away, and the data-ready interrupt work fits naturally here.

### EEPROM

**The EEPROM is not implemented today.** `eepromInit()` and `eepromTest()` do nothing, the read and write
calls are commented-out TODOs that report success, and `configblockInit()` is not called. Nothing is
persisted. If the magnetometer moves behind the IMU, I2C0 is free to host the EEPROM:

| I2C0 device | Address |
|---|---|
| EEPROM (24Cxx) | 0x50 to 0x57 |
| VL53L0X | 0x29 |
| BMP280 | 0x76 / 0x77 |
| QMC5883L (goes away) | 0x0D |

What has to be done:

1. A real driver: byte read, page write (typically 8 to 64 bytes), and acknowledge polling after each page,
   a write takes about 5 ms. `eeprom.c` is a stub.
2. `configblockeeprom.c` uses `I2C1_DEV`. Either enable I2C1 or switch it to `I2C0_DEV`.
3. Write **only while disarmed**. A page write blocks the bus for milliseconds, and the same bus carries the
   range finder.
4. Decide what to store: board identity, gyro bias and accel scale, trims, PID gains. The CRTP parameters
   would then need a storage layer ([code review](code-review.md#configuration)).

Do you need one? The ESP32 can keep settings in NVS (internal flash) at no cost. An external EEPROM is
worth the BOM line if the data must **survive a full chip erase** or travel with the board (serial number,
factory calibration). Internal flash writes also stall code that runs from flash, so they must only happen
on the ground as well.

### Blackbox storage (SD card)

Not present. Logging to a card from RAM is the safe way, see [code review](code-review.md#logging).
With the pins that are left:

| Option | Pins | Notes |
|---|---|---|
| SD over SPI on the **existing IMU bus** | one new chip select, for example 32 | cheapest, but it shares the bus with the IMU (`spi_drv.c` now supports several devices per bus) |
| SD over SPI on a **separate bus** | CLK 14, MOSI 13, MISO 35, CS 32 (pin matrix) | IMU timing stays isolated. Not available together with JTAG (13, 14). |
| SDMMC 1-bit | CLK 14, CMD 15, D0 2 | fixed pins on the ESP32, and GPIO2 is the buzzer |
| USB mass storage | n/a | the classic ESP32 has no USB device or host. The S3 does, see the [ESP32-S3 roadmap](esp32-s3-roadmap.md). |

A card makes 1 kHz logging easy (about 64 kB/s for 64-byte records), but cards stall for tens of
milliseconds now and then: buffer about a second in RAM and write from a low-priority task.
**Do not log to internal flash in flight.**

## Not on the board yet

| Wanted | Needs |
|---|---|
| IMU data-ready interrupt | one free input and the driver change above |
| UART for GPS or an RC receiver | pins for UART1/2, none are assigned in the firmware, only UART0 is used |
| Battery current sensing | one more ADC input |
| Blackbox storage | one of the SD options |
