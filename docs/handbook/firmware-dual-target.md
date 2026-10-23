# Firmware for two boards

One code base for the **FlyingFree V1.0** (classic ESP32, "FF01") and the new **FlyingFree V2** (ESP32-S3,
"FF02", see [board requirements](esp32-s3-board-requirements.md)). This page lists what in the code is tied
to the current board or to ESP-IDF 4.4, what can be taken from Betaflight and other GPL-compatible
projects, and the order of work.

The audit comes from reading the code (2026-10-23). **Step 1 (board abstraction) is done**: both boards build
from this tree on ESP-IDF 4.4.8 and the host tests pass, see [state](#state). Nothing has run on hardware.
Effort: **S** hours, **M** a day or two, **L** a week or more.

## State

| | FF01 (ESP32) | FF02 (ESP32-S3) |
|---|---|---|
| Builds (default, sysview, bench) | yes | yes |
| Board selection | follows the target: `idf.py set-target esp32` | `idf.py set-target esp32s3` |
| Sensors | BMI160 + QMC5883L, 1 kHz software timer (unchanged, on hold) | ICM-42688-P on the data-ready interrupt, MMC5983MA on SPI3, DPS310 on I2C0 |
| Battery | 3S / 4S, divider 15 k / 82 k | 1S / 2S / 3S, divider 10 k / 56 k, ESC current input |
| Console | UART0 | USB-Serial-JTAG |
| CPU clock | **240 MHz** (was 160 MHz by default) | 240 MHz |
| Run on hardware | not yet with this change | no board yet |

What changed for FF01: the settings `VSPI_PIN_*` became `IMU_SPI_PIN_*` and `ADC1_PIN` became `VBAT_ADC_PIN`
(same values), the CPU runs at 240 MHz (`sdkconfig.defaults.esp32`), the magnetometer value is now in gauss
(it was divided by its scale), and disabled LEDs work. `sensors_flyingfree_v01.c` got only these two
fixes; moving it onto `sensors_common.c` and the IMU interrupt waits for your board review.

## In short

* **ESP-IDF 4.4.8 does support the ESP32-S3.** This repository blocks it in five places (below). The reason
  to move to **ESP-IDF 5.x** is the drivers: the new RMT driver (DShot with a sync manager, bidirectional
  DShot), `adc_oneshot` with calibration, `i2c_master`, and TinyUSB mass storage. Port on the current ESP32
  first, so the S3 drivers are written once.
* About **70 % of the code is portable as-is**: controllers, estimators, commander, CRTP, log and param,
  utils, the ESP-NOW link.
* What is board-specific is concentrated in a few files: the motor driver, the ADC driver, the SPI layer,
  `sensors_flyingfree_v01.c`, `pm_esplane.h` and the Kconfig pin defaults.
* **Betaflight master has an ESP32-S3 port** (`src/platform/ESP32`, since April 2026). It is bare-metal
  without FreeRTOS and still excluded from Betaflight's CI, with bidirectional DShot, the magnetometer and
  MSP DisplayPort disabled. It is a register-level reference, not code to drop in. The protocol and filter
  code of Betaflight is the part worth taking.

## What blocked a second board

| Where | What | State |
|---|---|---|
| `tools/setup-idf.sh:52` | `install.sh esp32` only | fixed: `esp32,esp32s3` |
| `main/Kconfig.projbuild` | the board choice `depends on IDF_TARGET_ESP32` | fixed: `TARGET_FLYINGFREE_V02` for the S3, the default follows the target |
| `components/config/include/config.h` | `#error` unless the target is ESP32 | fixed: per-board guard in `board.h` |
| `.gitlab-ci.yml` | 4.4 image, ESP32 only | fixed: matrix esp32 / esp32s3, host tests first |
| `motors_brushless.h` | DShot on RMT channels 1-4, channel 4 is receive-only on the S3 | fixed: `BOARD_MOTOR_RMT_CHANNEL_0` (1 on ESP32, 0 on S3) |
| `spi_drv.c` | every device handle in one static VSPI handle | fixed: bus and device definitions, a handle per device, one mutex per bus |
| `components/core/crazyflie/CMakeLists.txt` | always built the FF01 sensor file | fixed: the board's sensor file only |

## Bugs found on the way

| Where | Bug | State |
|---|---|---|
| `spidev_esp32.c` | the audit reported that `spidevReadBits` never writes `*data` | **not a bug**, it does (line 97) |
| `sensors_flyingfree_v01.c` | magnetometer divided by `MAG_GAUSS_PER_LSB` instead of multiplied | fixed |
| `sensors_flyingfree_v01.c` | `CONFIG_BMP280_ENABLE` calls `ms5611Init`; `bmp280` has no `CMakeLists.txt` | open; the option is now FF01-only and says so |
| `adc_esp32.c` | `analogReadVoltage(pin)` ignored `pin` | fixed: GPIO to ADC1 channel, two pins, ADC2 path removed |
| `sensors.c` | NULL `.test` and `.manufacturingTest` called, `EXTI14_Callback` | fixed |
| `led_esp32.c` | a disabled LED did not compile (undefined Kconfig symbol used as a value) and reached `gpio_set_level(-1)` | fixed; on FF02 the system role moves to green, link-down to red |
| `platform_cf2.c` | hard-coded platform name `"ED12"` | fixed: renamed `platform_boards.c`, name from `board.h` |
| `sensors_flyingfree_v01.c:247` | `xTaskGetTickCount() < 2000` and `delay_ms = vTaskDelay` assume a 1 kHz tick | open (FF01 file on hold) |
| `stm32_legacy.h` | `M2T` exact only at 1 kHz | open |
| `eeprom.h` | raw flash address `0x3d2000` | open |
| `cpuid.c` | reads STM32 addresses | open (not called) |
| `deck_spi.c` | SPI2 with the IMU pins | open (not called); now names `IMU_SPI_PIN_*` |
| `sdkconfig.defaults` | no CPU frequency (160 MHz), no flash size | fixed in `sdkconfig.defaults.<target>` |

## Board abstraction

How a board is selected today: one Kconfig choice, the platform string `"0;FF01"`, one entry in the table in
`platform_cf2.c`, an `#ifdef` table in `sensors.c`, and three more `#ifdef CONFIG_TARGET_FLYINGFREE_V01` (ADC
channel, battery divider, axis signs).

The proposal:

| Piece | FF01 | FF02 |
|---|---|---|
| Kconfig choice | `TARGET_FLYINGFREE_V01`, `IDF_TARGET_ESP32` | `TARGET_FLYINGFREE_V02`, `IDF_TARGET_ESP32S3` |
| Target defaults | `sdkconfig.defaults` | plus `sdkconfig.defaults.esp32s3` (loaded by ESP-IDF automatically): USB-Serial-JTAG console, 8 MB flash, partition table, 240 MHz |
| Pin settings | role names instead of bus names: `IMU_SPI_*`, `MAG_SPI_*`, `MAG_CS`, `OSD_CS`, `IMU_INT1`, `UARTn_*`, `SDMMC_*`, `LEDSTRIP_PIN`, `VBAT_PIN`, `CURR_PIN`, `M5/M6` | same names, `range 0 48`, defaults from the [pin map](../../hardware/flyingfree-v2/README.md#pin-map) |
| Board header | `components/platform/boards/ff01.h` | `boards/ff02.h` |
| | RMT channels 1-4, IMU on SPI3, divider 15 k / 82 k | RMT 0-3, IMU on SPI2, mag and OSD on SPI3, divider 10 k / 56 k, current scale |
| | rotation matrix for the IMU and magnetometer, LED count, motor count | same |
| Platform table | `"FF01"` entry, `platform_cf2.c` renamed `platform_boards.c` | `"FF02"` entry |
| Sensors | `sensors_flyingfree_v01.c`: BMI160 + QMC5883L, soft timer | `sensors_flyingfree_v02.c`: ICM-42688-P + MMC5983MA + barometer, IMU interrupt |
| Common sensor code | `sensors_common.c`: gyro bias, accel scale, gravity alignment, low-pass filters, queues, task loop | shared |

The common part is today `static` inside `sensors_flyingfree_v01.c` (lines 136-206, 432-441, 487-634,
660-726) and has to be extracted. The alignment macros (`UPSIDEDOWN`, `IMU_X_TOFRAME`...) become a 3 x 3
rotation per sensor in the board header.

## ESP-IDF 5.x port

| Area | Today | In 5.x | Effort |
|---|---|---|---|
| DShot | legacy `driver/rmt.h`, `rmt_item32_t`, one shared static buffer in a header | `rmt_new_tx_channel` with a DShot encoder, a buffer per channel, `rmt_sync_manager` to start the 4 motors together. Keep the current waveform (two inversions that cancel) and check it on a scope. | M (TX), L (with bidirectional) |
| ADC | `adc1_config_width`, `esp_adc_cal` | `adc_oneshot` plus `adc_cali` curve fitting (S3) or line fitting (ESP32), GPIO to channel with `adc_oneshot_io_to_channel`, two channels | M |
| I2C | legacy `i2c_cmd_link` | works with warnings; move to `i2c_master` later, keeping the `i2cdev` API | M |
| SPI | `VSPI` naming | `SPI2_HOST` / `SPI3_HOST` as board constants | S |
| FreeRTOS names | `xQueueHandle`, `xTimerHandle`, `portTICK_RATE_MS` (about 40 places) | `QueueHandle_t`, `TimerHandle_t`, `portTICK_PERIOD_MS`; then drop `FREERTOS_ENABLE_BACKWARD_COMPATIBILITY` | S |
| Task placement | `STATIC_MEM_TASK_CREATE` and `xTaskCreate` without a core | a core argument, `xTaskCreateStaticPinnedToCore`: flight on core 1, radio and links on core 0 | S-M |
| Component REQUIRES | 4.4 adds `driver`, `esp_timer`, `esp_wifi`, `lwip`... implicitly | explicit REQUIRES in every component | S-M |
| Wi-Fi | `ESP_IF_WIFI_AP` | `WIFI_IF_AP` | S |
| Format strings | `uint32_t` printed with `%d` | `uint32_t` is `unsigned long` in 5.x: `PRIu32` | S-M |
| Host tests | stubs pinned to 4.4 | a second build with the 5.x `esp_now` callback | S |

Unchanged: LEDC (buzzer, brushed motors), `esp_timer`, the linker fragment for log and param, SystemView
(over the S3's USB-JTAG).

## What to take from other projects

All candidates are **GPL-3.0-or-later** (Betaflight, INAV, ArduPilot) or **BSD-3-Clause** (PX4), compatible
with this repository's GPL-3.0. Rules:

1. Keep every copied file's header.
2. Add a dated modification note (GPLv3 section 5a).
3. Add a `NOTICE` file naming the sources and the commit they came from (Betaflight `4b1aed7`, 2026-10-23).
4. Keep the BSD notice on anything from PX4.

Shims that let the Betaflight code compile with few edits:

| Betaflight call | Our implementation |
|---|---|
| `spiWriteReg`, `spiReadRegMsk`, `spiReadRegBuf`, `busReadRegisterBuffer`, `busWriteRegister`, `busBusy` | `spi_device_polling_transmit`, `i2cdev` |
| `micros`, `microsISR`, `delay`, `delayMicroseconds` | `esp_timer_get_time`, `vTaskDelay`, `esp_rom_delay_us` |
| `xxxConfig()` (parameter groups) | static configuration structs, later NVS |
| `FAST_CODE`, `FAST_DATA` | `IRAM_ATTR`, `DRAM_ATTR` |
| `serialPort_t` | a thin wrapper over the IDF UART driver and its event task |

### Sources

Paths in [Betaflight](https://github.com/betaflight/betaflight) master unless stated.

| Need | Source | Lines | Take | Effort |
|---|---|---|---|---|
| ICM-42688-P | `src/main/drivers/accgyro/accgyro_spi_icm426xx.c` | 537 | register init, anti-alias filter table (258/536/997/1962 Hz), ODR and range, 32 kHz CLKIN setup. The interrupt and DMA read path (`accgyro_mpu.c`) is rewritten as a GPIO ISR. | M |
| MMC5983MA | **not in Betaflight or INAV.** ArduPilot `libraries/AP_Compass/AP_Compass_MMC5xx3.cpp` (GPL-3.0+, I2C and SPI, set/reset state machine). PX4 `src/drivers/magnetometer/memsic/mmc5983ma/` (BSD-3) for comparison: its line 58 looks like a register mix-up, check against the datasheet. | 306 | the state machine, rewritten in C on the SPI shim | M |
| Barometer | `drivers/barometer/barometer_dps310.c` (DPS310, SPL07), `barometer_bmp388.c` (BMP388; check the BMP390 chip ID), `barometer_bmp280.c`. SPL06-001: INAV `barometer_spl06.c`. | 330-460 | whole driver | S |
| CRSF / ELRS | `src/main/rx/crsf.c`, `crsf_protocol.h` | ~1k | frame parser, CRC, channel unpacking, link statistics | S-M |
| SBUS | `src/main/rx/sbus.c`, `sbus_channels.c` | 334 | whole; inversion with `uart_set_line_inverse` | S |
| GPS | `src/main/io/gps.c` (UBX parts) | 3132 | UBX parser, checksum, CFG-VALSET builders. The start-up state machine is rewritten. | M |
| MSP v1/v2 | `src/main/msp/msp_serial.c`, `msp_protocol*.h` | ~1.3k | framing; `msp.c` only as a reference for the commands the air unit polls | S |
| MSP DisplayPort (DJI) | `src/main/io/displayport_msp.c` | 281 | the encoder | S |
| Analog OSD | `src/main/drivers/max7456.c`, `osd_symbols.h`, `io/displayport_max7456.c` | ~1.3k | register protocol, font upload; DMA segments replaced by queued SPI transactions. The OSD element engine (`src/main/osd/`) is large: write a small one. | M |
| SmartAudio, Tramp | `io/vtx_smartaudio.c`, `io/smartaudio_protocol.c`, `io/vtx_tramp.c`, `io/tramp_protocol.c` | ~2k | protocol and CRC, without the menu system | M |
| DShot logic | `src/main/drivers/dshot.c`, `dshot_command.c` | ~830 | packet and CRC, eRPM decode, command queue | S |
| Bidirectional DShot decode | `src/platform/common/stm32/pwm_output_dshot_shared.c` L194-258 (`decodeTelemetryPacket`), `drivers/dshot_bitbang_decode.c` | ~65 | GCR decode, fed from RMT RX durations. The RMT TX/RX turnaround is new code. | S (decode), L (transport) |
| RPM filter | `src/main/flight/rpm_filter.c` | 167 | whole | S |
| Gyro filters | `src/main/common/filter.c`, `filter.h` (PT1/PT2/PT3, state-variable notch) | 597 | whole; no biquad there in this snapshot | S |
| Dynamic notch | `src/main/flight/dyn_notch_filter.c`, `common/sdft.c` | 662 | whole | S |
| Blackbox | `src/main/blackbox/blackbox_encoding.c`, `common/encoding.c`, field tables from `blackbox.c` | ~600 + tables | encoders and a reduced header so Blackbox Explorer reads our logs; the I/O on SDMMC + FATFS is new | M |
| Battery | `src/main/sensors/battery.c`, `voltage.c`, `current.c` | ~1.4k | reference only, cell detection and warnings | S |
| CRC | `src/main/common/crc.c` | 131 | whole (`crc8_dvb_s2` for CRSF and MSP v2) | S |

Write ourselves: the UART HAL, the SDMMC blackbox writer, the I2S LED-strip driver (the IDF `led_strip`
component offers only RMT and SPI back-ends, both taken on FF02), M5/M6 on MCPWM or LEDC, the RMT
bidirectional DShot transport, and the board abstraction.

## Order of work

| Step | Content | Board | Effort | Done when |
|---|---|---|---|---|
| 0 | Bench-test what exists (P0 list in the [code review](code-review.md#safety)) | FF01 | S | arming, kill, failsafe, battery verified |
| 1 | **Board abstraction** on 4.4.8: V02 Kconfig, `board.h`, platform table, `sensors_common.c`, role-based SPI and the multi-device fix, ADC GPIO to channel, the FF02 sensor drivers, the bug list above. **Done 2026-10-23**, except the FreeRTOS renames and the core-pinned task macro (pinning is on hold). | both | M | FF01 flies as before (to check on the bench); FF02 builds with real drivers |
| 2 | **ESP-IDF 5.x port**: RMT DShot rewrite, `adc_oneshot`, explicit REQUIRES, format cleanup, CI matrix, setup script | FF01 | L (1-2 weeks) | FF01 flies on 5.x, DShot checked on a scope and on ESCs |
| 3 | **FF02 bring-up**: target defaults, pin map, ICM-42688-P with a data-ready ISR, MMC5983MA, barometer, DShot on RMT 0-3 | FF02 | L | FF02 hovers with the current feature set |
| 4 | **New peripherals**: UART HAL, CRSF and SBUS, GPS, MSP and DisplayPort, SD blackbox, LED strip, M5/M6, current sense | FF02 | L, M each | flies with ELRS, OSD in the goggles, logs readable in Blackbox Explorer |
| 5 | **Bidirectional DShot and RPM filter** | both | L | eRPM error rate measured while spinning |

Steps 1 and 2 can run while the PCB is designed and made, see the [PCB plan](../../hardware/flyingfree-v2/README.md).
