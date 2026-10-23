# ESP32-S3 board requirements

Requirements for the next flight-controller board: an ESP32-S3 board with the features of an **F4V3S**
class flight controller, powered from a **1S to 3S** battery. It replaces the FlyingFree V1.0 (classic
ESP32), which has run out of pins.

The open points were decided on 2026-10-23, see [decisions](#decisions). What still needs checking against
datasheets before the schematic is frozen is listed under [to verify](#to-verify-before-the-schematic-is-frozen).
Nothing here has been built. Pin numbers on this page are **ESP32-S3** GPIO numbers.

Priorities:

| | Meaning |
|---|---|
| **M** | must, the board is not usable without it |
| **S** | should, included unless pins or space force a trade |
| **C** | could, footprint or pads only if there is room |

## The reference: F4V3S

The F4V3S is a generic, widely cloned 30.5 mm STM32F405 board (Omnibus F4 SD style). Listings differ
between sellers and revisions, so this is the **common** feature set, not one exact product.

| Feature | F4V3S (typical) | Notes |
|---|---|---|
| MCU | STM32F405RGT6, 168 MHz | |
| IMU | MPU6000 on older boards, BMI270 or ICM-42688-P on "Plus" revisions, SPI | |
| Barometer | BMP280 | |
| OSD | AT7456E (MAX7456 compatible), analog | camera in and video out pads |
| Blackbox | microSD slot | some listings say on-board flash instead |
| Input | 2S to 6S | some listings say 2-4S or 3-6S |
| Regulators | 5 V 3 A BEC, 9 V pad on "Plus" | |
| Motor outputs | 4 | some listings show more |
| UARTs | 3 to 4, one with an SBUS inverter | |
| Other pads | LED strip, buzzer, current sensor, RSSI, I2C | |
| USB | micro-USB | |
| Size | 36 x 36 mm, 30.5 x 30.5 mm M3 holes, about 7 g | |

What changes on the new board:

* **1S to 3S** instead of 2S to 6S. 1S needs a **buck-boost** for the 5 V rail, because a loaded 1S cell is
  below 5 V. HD video needs 2S or more.
* The STM32 has more UARTs and timers. The S3 has **3 UARTs** and **4 RMT transmit channels**, which
  shape the I/O below.
* The S3 adds Wi-Fi, BLE and ESP-NOW (the control link of this project), native USB, and a second core.
* The board is **assembled by hand** with a stencil and a hot plate, see [assembly](#assembly).

## Decisions

| # | Topic | Decision | Consequence |
|---|---|---|---|
| 1 | Form factor | **36 x 36 mm, 30.5 mm holes** (F4V3S) | room for the module, SD slot, OSD chip and two buck-boosts |
| 2 | IMU | **ICM-42688-P** on SPI2 | lowest gyro noise of the candidates, the common Betaflight gyro; new driver needed |
| 3 | Magnetometer | **MMC5983MA on SPI3**, own chip select, sampled up to its 1 kHz continuous rate | SPI for a higher sampling rate than I2C. The BMM350 was dropped (1.27 mm WLCSP); the other pairs considered are listed [below](#imu-and-magnetometer-pairs-considered) |
| 4 | Analog OSD | **AT7456E footprint**, left unpopulated on HD-only builds | it shares SPI3 with the magnetometer, SD stays 1-bit, no expansion connector |
| 5 | Video | **analog VTX and camera, and DJI** | 9 V buck-boost rail, DJI only from 2S |
| 6 | Video supply | **9 V buck-boost from 2S-3S**, jumper for VBAT direct | off on 1S and on USB |
| 7 | Spare GPIO | **M5 / M6** outputs | all 39 GPIO are used; the magnetometer chip select costs the third status LED |
| 8 | Module | **ESP32-S3-MINI-1U-N8** | U.FL antenna, 8 MB flash, no PSRAM |
| 9 | SD bus width | **1-bit** | about 64 kB/s needed at 1 kHz, well within 1-bit |
| 10 | Regulators | **MP28167-A** for 5 V and 9 V, SOT-23 class buck for 3.3 V, low-noise LDO for the sensors | one buck-boost part for both rails |
| 11 | Assembly | **by hand, stencil and hot plate** | bottom-terminated parts on the top side only, no WLCSP or BGA |
| 12 | Barometer | kept from the F4V3S on I2C0 | I2C0 carries the barometer, external pads and the optional EEPROM |

## Mechanical

| ID | Requirement | Prio |
|---|---|---|
| MEC-01 | Outline at most **36 x 36 mm**, mounting holes **30.5 x 30.5 mm**, sized for M3 soft-mount grommets (4 mm holes) | M |
| MEC-02 | No components inside the grommet keep-out ring around each hole (about 6.5 mm diameter) | M |
| MEC-03 | Board 1.6 mm thick, top components at most 5 mm high, bottom at most 3 mm, so it stacks over a 4-in-1 ESC | S |
| MEC-04 | Weight at most **8 g** without wires and antenna | S |
| MEC-05 | USB-C and the BOOT button reachable on the board edge when the board sits in a stack | M |
| MEC-06 | IMU within 5 mm of the centre of the mounting holes, axes parallel to the board edges, orientation arrow on the silkscreen | M |
| MEC-07 | 2.4 GHz antenna through the module's **U.FL connector**, the antenna placed outside the carbon frame | M |
| MEC-08 | Barometer under open-cell foam, away from the LEDs and the air flow of the props | S |

## Assembly

| ID | Requirement | Prio |
|---|---|---|
| ASM-01 | Assembly with a **stencil, solder paste and a hot plate** (or reflow oven), rework with hot air. A stencil is ordered with every PCB revision. | M |
| ASM-02 | **Every bottom-terminated part on the top side** (module, IMU, magnetometer, barometer, QFN regulators), so one reflow pass on the hot plate does them all | M |
| ASM-03 | Bottom side only for parts an iron can solder: 0603 passives, connectors, the SD slot if it does not fit on top | M |
| ASM-04 | No WLCSP or BGA parts. Smallest package LGA or QFN with 0.5 mm pitch. | M |
| ASM-05 | Passives 0402 at the smallest, 0603 where space allows | S |
| ASM-06 | USB-C receptacle with through-hole shell legs (mechanical strength), USB 2.0 only (16 pin) | S |
| ASM-07 | No vias in pads under the IMU and the module; vias next to the pad, tented | S |

## MCU and module

| ID | Requirement | Prio |
|---|---|---|
| MCU-01 | **ESP32-S3-MINI-1U-N8**: 39 GPIO, 15.4 x 15.4 x 2.4 mm (LGA), U.FL antenna, 8 MB flash (two OTA images), no PSRAM, 3.3 V I/O | M |
| MCU-02 | Strapping pins **0, 3, 45, 46** have defined pull states at reset. GPIO0 goes to the BOOT button. Nothing drives 45 or 46 high at reset. | M |
| MCU-03 | Native USB (GPIO19/20) to the USB-C connector, with an ESD array and **5.1 k pull-downs on CC1 and CC2** so C-to-C cables supply power. It carries the console, USB-JTAG (SystemView, gdb) and later USB mass storage for the SD card. | M |
| MCU-04 | EN with an RC delay (or a regulator power-good) so the S3 starts after 3.3 V is stable, plus an EN pad or reset button | M |
| MCU-05 | All analog inputs on **ADC1 (GPIO1-10)**. ADC2 cannot be used while Wi-Fi is running. | M |
| MCU-06 | The IMU has **SPI2 to itself**. SPI3 serves the magnetometer and the OSD chip, each with its own chip select. | M |
| MCU-07 | The ROM prints boot messages on UART0 TX (GPIO43). The device on UART0 must tolerate it, or the eFuse that silences the ROM log is burnt in production. | S |

## Power

Input: one battery, 1S to 3S LiPo or LiHV, through the ESC harness or a VBAT pad.

| ID | Requirement | Prio |
|---|---|---|
| PWR-01 | Full operation of the 5 V and 3.3 V rails from **2.8 V to 13.2 V** at VBAT (1S under load to 3S LiHV full) | M |
| PWR-02 | A **TVS on VBAT**, SMBJ13A (13 V standoff, about 21.5 V clamp), so spikes stay below the MP28167-A's 22 V operating limit (26 V absolute). The SMBJ14A clamps at 23.2 V and was rejected. | M |
| PWR-03 | Under-voltage lockout around 2.6 V with hysteresis: no brown-out loops when a 1S pack sags | M |
| PWR-04 | **5 V rail**: MP28167-A buck-boost, 5.0 V +/-3 %, at least **2 A** for VBAT of 6 V and above, at least **1 A** at 2.8 V. Current limit, short-circuit and thermal protection. | M |
| PWR-05 | **3.3 V rail**: 2 A synchronous buck from the 5 V rail (SOT-23 class, TLV62569 or similar), at least **800 mA** load (S3 Wi-Fi transmit peaks around 500 mA, SD card writes, OSD chip), ripple under 30 mV, at least 22 uF next to the module | M |
| PWR-06 | **Sensor 3.3 V**: low-noise LDO from the 5 V rail (LP5907 class) for the IMU, magnetometer and barometer | S |
| PWR-07 | **9 V video rail**: a second MP28167-A, at least **2 A** for VBAT of 6 V and above. Enabled by a GPIO (PINIO, off at reset) **and** by a divider on EN that holds it off below about 6 V, so a 1S pack never runs it. Off on USB power. | M |
| PWR-08 | Solder jumper that selects the HD VTX connector supply: 9 V rail (default) or VBAT direct. A DJI O4 Air Unit (3.7-13.2 V) runs from VBAT even on 1S. | S |
| PWR-09 | **USB-only power**: the 3.3 V and 5 V rails run from USB VBUS through a Schottky diode to the 5 V rail. With a battery the 5 V rail sits above VBUS minus the diode drop, so nothing flows back into the USB host. | M |
| PWR-10 | **Battery voltage**: divider to ADC1, full scale at least 16 V for at most 2.5 V at the pin (about 10 k / 56 k), time constant at most 1 ms, error at most 1 % after a one-point calibration | M |
| PWR-11 | **Current**: CURR pad from the ESC (0-3.3 V) to ADC1, with a series resistor and a clamp to 3.3 V | M |
| PWR-12 | **Shielded inductors** on every converter (they sit on the same board as the magnetometer); 4.7 uH, 5 x 5 mm class with Isat at least 4 A for the buck-boosts | M |

The F4V3S offers 3 A at 5 V. From 1S that would draw over 6 A from the cell, so PWR-04 asks for less at low
input. The TPS63070, a common 1S buck-boost, takes only 2 V to 16 V and was rejected for its lack of
margin on 3S LiHV. The TPS55289 (3 V to 30 V) was rejected because it starts above a sagging 1S cell and
needs I2C to set its output.

### Power budget (estimate, to check against the chosen parts)

| Load | Rail | Typical | Peak |
|---|---|---|---|
| ESP32-S3 with Wi-Fi | 3.3 V | 100 mA | 500 mA |
| IMU, barometer, magnetometer | 3.3 V sensors | 5 mA | 10 mA |
| microSD card | 3.3 V | 30 mA | 150 mA |
| OSD chip | per datasheet | 30 mA | 50 mA |
| Receiver (ELRS) | 5 V | 100 mA | 150 mA |
| GPS | 5 V | 50 mA | 100 mA |
| LED strip, 8 x WS2812 | 5 V | 160 mA | 480 mA |
| Buzzer | 5 V | 30 mA | 40 mA |
| Analog camera | 5 V | 150 mA | 200 mA |
| Analog VTX | 5 V or 9 V | per its power level | |
| DJI air unit | 9 V or VBAT | several watts | see the unit's datasheet |

Total on 5 V, including the 3.3 V rail: about 0.7 A typical and 1.7 A peak, inside PWR-04.

## Sensors

| ID | Requirement | Prio |
|---|---|---|
| SEN-01 | **ICM-42688-P** (LGA-14, 2.5 x 3 mm) on SPI2, **INT1 data-ready on a GPIO with no other use** | M |
| SEN-02 | ICM-42688-P pin INT2/FSYNC/CLKIN on a second GPIO, usable as a second interrupt or to feed a 32 kHz clock from the S3 | S |
| SEN-03 | **MMC5983MA** (LGA-16, 3 x 3 mm) on **SPI3** with its own chip select, continuous mode up to 1 kHz with automatic set/reset. Its INT pin goes to a test pad only; the firmware polls it at the output rate. | S |
| SEN-04 | Magnetometer at least 15 mm from every inductor and from the VBAT and motor-current paths | S |
| SEN-05 | External compass still possible: I2C pads (COM-04) for the compass inside a GPS module, in case the on-board one is disturbed by the power stage | S |
| SEN-06 | Barometer on I2C0: BMP280 as on the F4V3S, or a newer LGA part (BMP390, DPS310, SPL06-001 class) | M |
| SEN-07 | Board temperature from the S3's internal sensor, no part needed | C |

Buy the ICM-42688-P from authorised distributors only: counterfeit parts have been on the market.

A magnetometer sampled at up to 1 kHz lets the firmware filter the field the motor currents add (the CURR
input gives the current to correlate with). Noise rises with the bandwidth setting, so pick the rate from
the MMC5983MA datasheet table, not the maximum by default.

### IMU and magnetometer pairs considered

| Pair | Magnetometer path | Why not chosen |
|---|---|---|
| BMI270 + BMM350 | IMU aux bus | BMM350 is a 1.27 mm WLCSP, and its 12-byte data block exceeds the aux interface's 8-byte burst (Bosch forum) |
| BMI270 + MMC5983MA | IMU aux bus | aux setup with a non-Bosch part has no reference design, and the aux path is I2C, against the SPI requirement |
| LSM6DSV16X + LIS2MDL | IMU sensor hub | hub reported hard to get working, I2C path, 2 x 2 mm magnetometer, no driver here |
| BMI088 (+ BMP388) + magnetometer | separate | two chip selects cost a GPIO more, larger package. The Crazyflie sensor layer for it is in git history (commit `d85a9a7`) if the choice is revisited. |
| ICM-45686 + magnetometer | IMU aux master | newest part, immature drivers, higher gyro noise than the ICM-42688-P |
| QMC5883L or IST8310 | any | modest resolution and many clones (QMC5883L), single-measurement mode only (IST8310) |

## Motor and servo outputs

| ID | Requirement | Prio |
|---|---|---|
| MOT-01 | **4 motor outputs**, DShot150 to DShot600 on RMT transmit channels 0-3 | M |
| MOT-02 | **Motor outputs stay low from reset until the firmware drives them.** No strapping pins, no UART0 TX, no pins that toggle at boot. GPIO1-18 glitch for about 60 us at power-up (chip datasheet table 2-2), so motors, servos and the 9 V enable use GPIO21 and 33-38. | M |
| MOT-03 | **Bidirectional DShot** possible: no pull resistors on the motor lines (the idle level inverts), and the 4 RMT receive channels are kept free for the eRPM replies | S |
| MOT-04 | Series resistor (about 100 ohm) and ESD protection on each motor line at the connector | S |
| MOT-05 | **M5 and M6** outputs for servos or PWM devices, on MCPWM or LEDC. They cannot be DShot, because all 4 RMT transmit channels are taken. Same boot rule as MOT-02. | S |

## Serial interfaces and radio

| ID | Requirement | Prio |
|---|---|---|
| COM-01 | **3 UARTs** on pads, each with 5 V and GND beside it: receiver (CRSF/ELRS or SBUS), GPS, VTX (MSP DisplayPort to the DJI unit). The console moves to USB. | M |
| COM-02 | SBUS without an external inverter: the S3 UART inverts in hardware. Series resistor on every RX pad. | M |
| COM-03 | One UART can run single-wire half-duplex (SmartAudio, Tramp) for the analog VTX | S |
| COM-04 | External I2C pads (SDA, SCL, 5 V, GND) on I2C0, with 2.2 k to 4.7 k pull-ups to 3.3 V on the board | M |
| COM-05 | 2.4 GHz Wi-Fi, BLE and ESP-NOW for the [control link](espnow-link.md), configuration and telemetry. The antenna is covered by MEC-07. | M |

The F4V3S has a 4th UART. With 3 on the S3, ESC telemetry comes from bidirectional DShot instead of its
own UART. The analog VTX control (COM-03) and the DJI MSP link share the VTX UART, since only one video
system flies at a time.

## Storage

| ID | Requirement | Prio |
|---|---|---|
| STO-01 | **microSD slot** on the SDMMC host, **1-bit** (CLK, CMD, D0). SDHC up to 32 GB, FAT32. Push-push socket, Hirose DM3AT class. | M |
| STO-02 | 10 uF or more on the card supply, close to the slot, so inserting a card does not reset the S3 | S |
| STO-03 | EEPROM footprint (24Cxx, SOIC-8 or TSSOP-8) on I2C0 for board identity and factory calibration. Settings go to NVS in the module flash. | C |

The firmware must not log to the module flash in flight, because flash writes stall the cache. There is no
pin left for a card-detect switch; the firmware checks for the card at boot.

## Video and OSD

| ID | Requirement | Prio |
|---|---|---|
| VID-01 | **HD VTX connector**, 6-pin JST-SH 1.0 mm, in the DJI order (power, GND, TX, RX, GND, receiver signal), on the VTX UART, powered per PWR-08 | M |
| VID-02 | **Analog OSD**: AT7456E footprint on SPI3 (shared with the magnetometer), with camera-in, video-out and video-ground pads. Supplied from 5 V; its SDOUT reaches the shared MISO through a 3.3 V tri-state buffer enabled by its chip select. Top side (exposed pad). Left unpopulated on HD-only builds. | M |
| VID-03 | Camera power pad (5 V) and analog VTX power pad (5 V or 9 V by solder jumper) | M |
| VID-04 | No camera input and no video processing on the flight S3, see the [roadmap](esp32-s3-roadmap.md#1-camera-overlay-osd) | M |

## Indicators and user I/O

| ID | Requirement | Prio |
|---|---|---|
| IND-01 | Power LED on 3.3 V | M |
| IND-02 | 2 status LEDs (the firmware uses red, green and blue today; blue goes). GPIO45/46 suit them, since an LED to ground keeps the strapping level low. | M |
| IND-03 | Buzzer pads (5 V and switched ground), low-side MOSFET, flyback diode, at least 100 mA | M |
| IND-04 | LED-strip pad (5 V, GND, DIN) with a 3.3 V to 5 V buffer (74AHCT1G125 class). It is driven by I2S, since all 4 RMT transmit channels go to the motors and SPI3 goes to the magnetometer and OSD. | S |
| IND-05 | BOOT button on GPIO0 | M |

## Protection, layout and EMC

| ID | Requirement | Prio |
|---|---|---|
| EMC-01 | At least 4 layers, an unbroken ground plane under the IMU, magnetometer and module | M |
| EMC-02 | Switching regulators and their inductors at least 10 mm from the IMU and barometer, 15 mm from the magnetometer (SEN-04) | S |
| EMC-03 | ESD protection on USB (M) and on the external UART, I2C and receiver pads (S) | M/S |
| EMC-04 | Ferrite or LC filter on the 9 V rail and on the camera supply | S |
| EMC-05 | Regulators deliver full rated current for 10 minutes at 40 C ambient without thermal shutdown | S |
| EMC-06 | 2.4 GHz coexistence: the S3 antenna and a 2.4 GHz ELRS antenna sit as far apart as the frame allows. Check the range of both links with both radios on. | S |

## Test and manufacturing

| ID | Requirement | Prio |
|---|---|---|
| TST-01 | Test points on every rail (VBAT, 9 V, 5 V, 3.3 V, sensor 3.3 V) and GND | M |
| TST-02 | Every pad labelled on the silkscreen, plus the board name, revision and IMU arrow | M |
| TST-03 | Pads for 3.3 V, GND, EN, GPIO0, UART0 RX/TX to flash without USB | S |
| TST-04 | Board revision readable by the firmware through the EEPROM (STO-03) when fitted, to replace the hard-coded platform string `0;FF01` | C |
| TST-05 | JTAG pads (GPIO39-42) only if those pins end up unused. USB-JTAG covers debugging. | C |

## GPIO budget

The MINI-1U exposes 39 GPIO, including USB (19, 20), the strapping pins (0, 3, 45, 46) and UART0 (43, 44).

| Function | Pins | Pin constraints |
|---|---|---|
| USB D- / D+ | 2 | fixed, 19 / 20 |
| BOOT button | 1 | fixed, 0 |
| ICM-42688-P SPI2 (SCK, MOSI, MISO, CS), INT1, INT2/CLKIN | 6 | IOMUX pins 10-13 give the shortest path; INT1 on a pin used for nothing else |
| I2C0 (barometer, external pads, EEPROM) | 2 | any |
| Motors 1-4 | 4 | quiet at power-up: 33-36 |
| M5, M6 | 2 | quiet at power-up: 37, 38 |
| UART0, 1, 2 | 6 | UART0 on 43 / 44 |
| VBAT, CURR | 2 | ADC1, GPIO1-10 |
| microSD, 1-bit (CLK, CMD, D0) | 3 | any |
| SPI3 (SCK, MOSI, MISO), CS magnetometer, CS OSD | 5 | any |
| Buzzer | 1 | any |
| Status LEDs | 2 | 45 / 46 |
| LED strip | 1 | any |
| PINIO: 9 V rail enable, spare | 2 | 9 V enable quiet at power-up: 21 |
| **Total** | **39 of 39** | |

Not fitted, for lack of pins: a third status LED, the magnetometer interrupt, RSSI, rail monitor, card
detect, expansion connector, 4-bit SD.

**No pin is spare.** If the layout needs one, give up M6 first.

## Beyond the F4V3S

Features the F4V3S lacks, or this project's firmware does not have yet.

| Feature | Board | Firmware today |
|---|---|---|
| Native USB-C: console, USB-JTAG, SD as a USB drive | MCU-03 | console on UART0, JTAG by external probe |
| Wi-Fi, BLE, ESP-NOW control link | COM-05 | ESP-NOW link done |
| IMU data-ready interrupt | SEN-01 | 1 kHz software timer, change on hold |
| Bidirectional DShot and RPM filter | MOT-03 | DShot out only |
| Magnetometer on SPI at up to 1 kHz | SEN-03 | QMC5883L on I2C0, to be replaced by an MMC5983MA driver |
| ICM-42688-P IMU | SEN-01 | BMI160 driver, to be replaced |
| GPS | COM-01, COM-04 | none, roadmap phase 4 |
| VTX power switch and pit mode | PWR-07 | none |
| Persistent settings, board identity | STO-03 | EEPROM stub, NVS unused |

Left out on purpose: an integrated ELRS receiver (another MCU and a second 2.4 GHz radio next to the S3),
an integrated ESC, a dual IMU, PSRAM and an on-board charger.

## To verify before the schematic is frozen

| Item | Why |
|---|---|
| MMC5983MA SPI: mode, maximum clock, how SPI or I2C is selected at power-up | SEN-03 |
| MMC5983MA noise at the chosen bandwidth and rate | SEN-03 |
| SPI3 timing with the magnetometer and the OSD both active | MCU-06, VID-02 |
| MP28167-A absolute maximum input against the TVS clamp | PWR-02 |
| MP28167-A with voltage on its output and no input (USB-only power) | PWR-09, no reverse current into VBAT |
| MP28167-A start-up at 2.8 V and the EN divider of the 9 V rail | PWR-01, PWR-07 |
| Input range and power of the exact DJI unit on 2S | PWR-07 |
| Top-side placement study in 36 x 36 mm | done with typical sizes, passes: see the [PCB plan](../../hardware/flyingfree-v2/README.md#placement-study) |
| IO33-37 come up as inputs on the N8 (the module notes an eFuse-decided default) | MOT-02, first board |

## Verification at bring-up

| Check | Pass |
|---|---|
| Sweep VBAT 2.8 V to 13.2 V with full load on the 5 V and 3.3 V rails | rails in tolerance, no reset |
| 9 V rail with a 1S, 2S and 3S pack | off on 1S, in regulation at 2 A on 2S and 3S |
| Wi-Fi transmitting continuously, 3.3 V on a scope | no droop below 3.1 V, no brown-out |
| Spike on VBAT (ESC disconnected) | no damage, TVS clamps below the parts' ratings |
| Reset and power-up with the ESC attached, motor lines on a scope | no pulse on any motor line, M5 or M6 before arming |
| USB only | the board enumerates and boots, 9 V off, no voltage on VBAT |
| Magnetometer with motors spinning at hover throttle | heading error to be set after the first measurements |
| Battery reading against a meter, 1S to 3S | at most 1 % error after calibration |
| SD write at 64 kB/s for 30 minutes during flight | no dropped records |
