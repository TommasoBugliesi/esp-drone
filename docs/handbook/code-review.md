# Code review

What was found, what has been fixed, and what is worth doing next. Findings come from reading the code and
from building it, not from flying it.

* [Status of the findings](#status-of-the-findings)
* [Proposals](#proposals), grouped by area, with priority and effort
* [Suggested order](#suggested-order)

Effort: **S** hours, **M** a day or two, **L** a week or more. Priority: **P0** safety, **P1** needed for a
good flight controller, **P2** worthwhile, **P3** later.

## Status of the findings

| # | Finding | State |
|---|---|---|
| 1 | Every CRTP setpoint cleared the emergency stop (buttons byte read past the packet), which also defeated tumble detection | fixed |
| 2 | ESP-NOW callback sent an uninitialised packet, signed/unsigned mismatch with the decoder | fixed, replaced by [the new link](espnow-link.md) |
| 3 | Motor function table cross-wired (brushed and brushless pointers swapped) | fixed |
| 4 | Low/critical battery thresholds were per cell but compared with the pack voltage, so never triggered on 3S/4S | fixed, now settings |
| 5 | Selecting the Kalman estimator crashed (its task is not started) | fixed, falls back with a warning |
| 6 | QMC5883L read with the HMC5883L layout (byte order and axis order) | fixed, **axis remap needs checking on the board** |
| 7 | DShot arming: loop counted twice and sent a burst, no frames before the stabilizer starts, shared buffer without a lock | fixed, **not tested on ESCs** |
| 8 | Five different thrust limits | fixed, one setting `MOTOR_THRUST_MAX`. The mixer still has no desaturation, see below |
| 9 | Yaw gains are zero (yaw is uncontrolled) | **not changed**, it may be deliberate. The gains are now settings. |
| - | `main` did not build from a clean checkout (SystemView needed a local sdkconfig, the `esp-now` component fails on 4.4, a missing include) | fixed |
| - | 87 dead files, STM32 macros, branches for removed boards | removed |

## Proposals

### Safety

| Prio | Proposal | Why | Effort |
|---|---|---|---|
| P0 | **Hardware watchdog on the stabilizer**: subscribe the stabilizer and sensor tasks to the ESP-IDF task watchdog, and make the panic handler put the motors at minimum throttle | Today a hung or crashed control loop leaves the last motor command until the ESCs time out. The keepalive covers a stall, not a crash. | S-M |
| P0 | **DShot disarm on kill and on failsafe**: send command 0 (motor stop) for a few frames instead of 48 | Kill currently leaves the ESCs armed at 0 % throttle | S |
| P0 | **Bench-test the safety paths** before anything else: arm, kill, link loss, ESP-NOW range, battery low | Everything in the [verified table](development.md#what-is-verified-and-what-is-not) marked "no" | S |
| P0 | **Change the default Wi-Fi password** (`12345678`) and require one | The soft AP accepts anyone who knows it, and the CRTP/UDP path has no authentication | S |
| P1 | **Act on a critical battery**: `pmSystemShutdown()` is empty, critical voltage only raises a LED/sound. Reduce the thrust ceiling at low, descend at critical. | A flying pack that is flat gives no warning to the controller | S-M |
| P1 | **Crash on a full CRTP queue**: `crtpRxTask` calls `ASSERT(0)` when a port queue is full. Drop the packet and count it. | A burst of packets from a client can reboot the drone in flight | S |
| P1 | **Mixer desaturation**: when one motor saturates, shift or scale all four so the differential survives. There is a TODO in `power_distribution_stock.c`. | At low thrust, roll/pitch/yaw demands clip asymmetrically and cause drift | S-M |

### Dual core and timing

> These two are on hold while the board and sensor choice are reviewed.

| Prio | Proposal | Why | Effort |
|---|---|---|---|
| P1 | **Sensor tick from the IMU interrupt**, not from a software timer | The 1 kHz tick is a FreeRTOS software timer running in the timer service task, which runs at **priority 1** (verified in the build configuration) below Wi-Fi. It shares that task with the LED, sound and log timers. Result: jitter on the loop that stabilises the drone. The BMI160 samples at 1600 Hz and is read at 1 kHz, so samples are skipped and the timestamp is taken when the task wakes, not when the sample was taken. | M |
| P1 | **Pin tasks to cores**: sensors, stabilizer, DShot output on core 1; Wi-Fi, CRTP, logging, params on core 0 | No task is pinned today. ESP-IDF runs Wi-Fi on core 0 and the flight tasks can land there. The interrupt for RMT, SPI and GPIO is bound to the core that ran its init, which is currently arbitrary. | M |
| P2 | **Measure first**: enable the run-time statistics (already on) and log per-core load and the stabilizer period jitter | Gives a baseline to judge the two items above | S |
| P2 | Run the **SPI at 10 MHz** and use polling transmit for the 13-byte IMU read | The bus is configured for 1 MHz although the BMI160 supports 10 MHz. The VLA buffers in `spidevRead` are on the stack. | S |
| P2 | **Put flight code and ISRs in IRAM** and avoid flash writes in flight | Flash writes stall code that runs from the flash cache | M |

### Estimation and control

| Prio | Proposal | Why | Effort |
|---|---|---|---|
| P1 | **Review the Mahony gains** in `sensfusion6.c`: Kp 4.0 and Ki 1.0 (upstream 0.4 and 0.001) | A large integral gain in an attitude filter can drift or oscillate. It may have been set while debugging. | S |
| P1 | **Decide on yaw**: set real yaw gains or document that yaw is passive | See finding 9 | S |
| P2 | **Gyro range**: the BMI160 runs at +-2000 deg/s, 16.4 LSB per deg/s. +-1000 or +-500 halves or quarters the quantisation for a drone that rarely exceeds that. The bias variance threshold (8000) was tuned for another scale. | Resolution | S |
| P2 | **Altitude and position hold**: `thrustBase` is 50, 2 DShot steps above idle, so these modes cannot hold the vehicle. The Kalman filter is not started and there is no working barometer. | They are enabled in Kconfig but cannot work yet | L |
| P3 | Gyro **notch / RPM filtering** with bidirectional DShot telemetry | Standard in Betaflight, needs the DShot RX side of the RMT | L |

### Logging

| Prio | Proposal | Why | Effort |
|---|---|---|---|
| P1 | **Blackbox**: the stabilizer writes a fixed-size binary record every k-th iteration into a lock-free ring buffer, a low-priority task on core 0 drains it to an SD card and/or a downlink. Add a header with a version and field table so a dashboard can parse any version. | The CRTP log reads variables from a timer at most every 10 ms, without a snapshot, so a record can mix two stabilizer iterations. You cannot capture the 500 Hz to 1 kHz data that PID and filter tuning need. | M-L |
| P1 | **Count dropped log packets** at every stage and add a sequence number | Packets are dropped silently in three places (worker queue of 5, CRTP queue, UDP queue of 16), the dashboard cannot tell | S |
| P2 | **Stop the log timers from sharing the sensor tick's task**, or move them to a dedicated task | They steal time from each other today | S |
| P2 | Grow the ESP-NOW **status frame** into a decimated telemetry downlink | Keeps the log traffic off the Wi-Fi channel the control link shares | M |
| P3 | Serve the blackbox files over HTTP from the soft AP when disarmed | Reading a card without removing it | M |

### Configuration

| Prio | Proposal | Why | Effort |
|---|---|---|---|
| P1 | **Persistent settings in NVS**: a parameter store behind the CRTP params (PID gains, trims, calibration), written only while disarmed | Parameters reset at reboot, so live tuning is lost. This is the missing half of a Betaflight-style configurator. | M |
| P2 | **CLI or JSON over the link** to read and write settings, so a configurator needs no rebuild | Kconfig is build time only | M |
| P2 | **Persist gyro bias and accel scale** after a good calibration, validated by temperature | Start-up is currently delayed until 1024 steady samples are found | S-M |
| P3 | A real **EEPROM driver**, only if data must survive a chip erase, see [hardware](hardware.md#eeprom) | The current one is a stub | M |

### Structure and tooling

| Prio | Proposal | Why | Effort |
|---|---|---|---|
| P1 | **Add more host tests**: PID, mixer, filters, CRC, the ESP-NOW task loop | Only the link state machine is tested. These modules are pure logic and need no hardware. | M |
| P1 | **Run the tests and the transmitter build in CI** | CI builds the firmware only | S |
| P2 | **Rename `stm32_legacy.h`** (38 includers, it holds the `M2T` family and a few typedefs); keep only what is used. (`platform_cf2.c` is now `platform_boards.c`.) | It says STM32 in an ESP32 project. Wait until the sensor work lands, it touches the same files. | S |
| P2 | **Split the `crazyflie` component** (hal, modules, utils) into smaller components with explicit dependencies | One 20 000 line component makes everything depend on everything | L |
| P2 | **Fix the 10 compiler warnings**, enable `-Werror` in CI | Warnings hide new ones | S |
| P2 | **Move `_dshotCmd` out of `motors_brushless.h`**; a `static` array in a header gives every includer its own copy and a warning | | S |
| P2 | **Disabled LEDs**: `ledSet()` calls `gpio_set_level(-1)` for an LED that is turned off in Kconfig, which logs an error on every LED sequence step | | S |
| P3 | **Move `docs/` upstream material and old board PDFs** (about 57 MB, most of the 51 MB repository) out of the tree, or into Git LFS | Clone size | S |
| P3 | Translate or retire `README_cn.md`, it still describes the upstream project | | S |

### Flash size

The image is 796 800 bytes of the 1 MB application partition, 24 % free. That is enough for the current
features but not for a GPS stack, a CLI and an OSD. Switch to a larger single-app partition
(`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`, 1.5 MB) before adding features, and decide early whether you want
OTA updates, which need two app partitions and so a 4 MB flash or more.

## Suggested order

1. **Bench-test what exists**: arming, DShot, the link, failsafe, kill, battery (all P0, no code).
2. **Watchdog, DShot disarm, queue assert, default password**: small, high value.
3. **Your sensor and board review**, including the interrupt-driven tick and core pinning.
4. **Persistent settings** and the **blackbox**: they unlock tuning and a real configurator.
5. **Tests and CI** grow with every item above.
6. Then estimation work (Mahony, yaw, gyro range) with the blackbox to measure it.

## Where the numbers come from

* Timer service priority 1 and dual-core configuration: the generated `sdkconfig` of the default build.
* Image size: the build output (`ESPDrone.bin` 0xC2880 bytes, 0x3D780 free).
* Gains and limits: `sensfusion6.c`, `position_controller_pid.c`, `spi_drv.c`.
* Dropped-packet points: `worker.c`, `crtp.c`, `wifi_esp32.c`.
