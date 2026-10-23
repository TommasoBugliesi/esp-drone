# ESPDrone handbook

Documentation for the FlyingFree V1.0 flight-controller firmware. Pages are short and independent,
read what you need.

## I want to...

| ...do this | Go to |
|---|---|
| Build and flash the firmware | [Getting started](getting-started.md) |
| Understand what runs when, and why | [Architecture](architecture.md) |
| Change pins, motors, battery, PID gains | [Configuration](configuration.md) |
| Fly with an ESP32 transmitter or a phone bridge | [ESP-NOW link](espnow-link.md) |
| See the pin map, or choose an IMU / EEPROM / SD card | [Hardware](hardware.md) |
| Contribute, run the tests, see what changed | [Development](development.md) |
| Know what is wrong or could be better | [Code review](code-review.md) |
| Move to an ESP32-S3 with GPS, video and OSD | [ESP32-S3 roadmap](esp32-s3-roadmap.md) |
| Specify the ESP32-S3 board (F4V3S features, 1S-3S) | [ESP32-S3 board requirements](esp32-s3-board-requirements.md) |
| Run one firmware on both boards, port Betaflight code | [Firmware for two boards](firmware-dual-target.md) |
| Design and build the new PCB | [PCB plan](../../hardware/flyingfree-v2/README.md) |

## Terms used

| Term | Meaning |
|---|---|
| **CRTP** | Crazyflie Real-Time Protocol, the packet format cfclient and the log/param services use |
| **Setpoint** | What the pilot asks for: roll, pitch, yaw rate and thrust |
| **Commander** | Module that holds the current setpoint and applies the link-loss watchdogs |
| **Stabilizer** | The 1 kHz task: estimator, controllers, mixer, motor output |
| **Mixer** | Turns roll/pitch/yaw/thrust into four motor commands (`power_distribution_stock.c`) |
| **DShot** | Digital ESC protocol, throttle 48 (0 %) to 2047 (100 %), 0 is the disarm command |
| **ESP-NOW** | Espressif's connectionless 2.4 GHz frames, used here as the control link |
| **Kconfig / sdkconfig** | ESP-IDF's build-time configuration system and its result file |

## Conventions

* Pin numbers are GPIO numbers of the board's chip (ESP32 on V1.0, ESP32-S3 on V2).
* Gains in Kconfig are stored times 1000 (`75000` means 75.0).
* "Not verified on hardware" is written wherever something has only been compiled or unit tested.
