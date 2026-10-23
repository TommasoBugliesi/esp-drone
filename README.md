# ESPDrone for FlyingFree

Flight-controller firmware for a quadcopter built on the **ESP32**. It started as
Espressif's [ESP-Drone](https://github.com/espressif/esp-drone), itself a port of the
[Crazyflie](https://github.com/bitcraze/crazyflie-firmware) firmware (STM32). This fork targets the
custom **FlyingFree V1.0** board: BMI160 IMU over SPI, brushless motors driven with DShot, 3S/4S battery.

| | |
|---|---|
| Chip | ESP32 (dual core), ESP-IDF **v4.4.8** |
| Control loop | 1 kHz stabilizer, 500 Hz attitude, complementary estimator |
| Motors | DShot150/300/600 through the RMT peripheral |
| Remote control | **ESP-NOW** link to a transmitter (this repo has a reference one), or Wi-Fi/UDP with cfclient |
| Configuration | Kconfig with a flight-controller style menu, a schema exporter for building a UI |

> **Status.** The firmware builds and the unit tests pass. It has **not been flown or powered up since the
> refactor**: motor arming, the ESP-NOW link and the new failsafe behaviour need bench testing with the
> propellers off. See [what is verified](docs/handbook/development.md#what-is-verified-and-what-is-not).

## Quick start

```bash
tools/setup-idf.sh                    # once: installs ESP-IDF 4.4.8 under ~/esp, no root needed
. ~/esp/idf-env.sh                    # in every new shell

idf.py set-target esp32
idf.py menuconfig                     # ESPDrone Config: pins, sensors, motors, link, PID gains
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

First power-up with the propellers **off**, using the bench profile (low throttle ceiling, boots disarmed):

```bash
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;profiles/bench.defaults" reconfigure build
```

## Repository layout

```
main/                        app_main, Kconfig (all ESPDrone options)
components/
  core/crazyflie/            flight code: hal/ (sensors, power, links), modules/ (stabilizer,
                             controllers, estimators, commander, CRTP), utils/
  drivers/                   motors (DShot, brushed), BMI160, QMC5883L, VL53L0X, SPI/I2C buses, ...
  platform/ config/          board identification, task priorities and stack sizes
  lib/dsp_lib/               matrix / math routines used by the Kalman filter
profiles/                    sdkconfig layers: sysview.defaults, bench.defaults
tools/                       setup-idf.sh, config/espdrone_config.py (Kconfig <-> UI bridge)
examples/espnow_transmitter/ reference transmitter, a separate ESP-IDF project
tests/host/                  unit tests that run on the PC with plain gcc
docs/handbook/               the documentation, start here
docs/en, docs/zh_CN          legacy upstream ESP-Drone docs (other boards), kept for reference
hardware/                    schematics and datasheets of the upstream boards and sensors
```

## Documentation

Start with the **[handbook](docs/handbook/README.md)**:

| Page | Read it to |
|---|---|
| [Getting started](docs/handbook/getting-started.md) | install, build, flash, first power-up |
| [Architecture](docs/handbook/architecture.md) | boot sequence, tasks, control path, safety model |
| [Configuration](docs/handbook/configuration.md) | every setting, profiles, building a configurator UI |
| [ESP-NOW link](docs/handbook/espnow-link.md) | protocol, arming, failsafe, writing a transmitter |
| [Hardware](docs/handbook/hardware.md) | FlyingFree pin map, open decisions (IMU, EEPROM, SD card) |
| [Development](docs/handbook/development.md) | workflow, tests, what changed in this fork |
| [Code review](docs/handbook/code-review.md) | findings and improvement proposals |
| [ESP32-S3 roadmap](docs/handbook/esp32-s3-roadmap.md) | what is missing to treat an S3 like an STM32 flight controller |

## License and credits

GPL-3.0, see [LICENSE](LICENSE). Flight code from the [Crazyflie firmware](https://github.com/bitcraze/crazyflie-firmware/tree/2021.01)
(Bitcraze, GPL-3.0), ESP32 port by Espressif Systems, math routines from
[esp32-lin](https://github.com/whyengineer/esp32-lin/tree/master/components/dsp_lib).
