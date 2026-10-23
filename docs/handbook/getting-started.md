# Getting started

From a clean machine to firmware on the board.

## 1. Requirements

* Linux with `git`, `python3` and `curl`. About 3 GB of disk, an internet connection for the first install.
* No root needed. ESP-IDF, its toolchain, CMake and Ninja are installed under your home directory.

## 2. Install ESP-IDF

```bash
tools/setup-idf.sh
```

This installs **ESP-IDF v4.4.8** into `~/esp/esp-idf`, the ESP32 toolchain into `~/.espressif`, and writes
`~/esp/idf-env.sh`. It can be run again, finished steps are skipped.

> **Why not a newer ESP-IDF?** The project uses the legacy RMT, ADC and I2C drivers of 4.4. A newer
> release needs a port, which is also the first step of the [ESP32-S3 roadmap](esp32-s3-roadmap.md).
>
> ESP-IDF 4.4 does not support Python 3.12 or newer (Ubuntu 24.04 ships 3.12). The script detects this
> and fetches a private Python 3.10 with `uv`, nothing system-wide is changed.

Activate it in every new shell:

```bash
. ~/esp/idf-env.sh
idf.py --version        # ESP-IDF v4.4.8
```

## 3. Build

The target chip selects the board:

```bash
idf.py set-target esp32       # FlyingFree V1.0
idf.py set-target esp32s3     # FlyingFree V2 (ESP32-S3)
idf.py build
```

Changing the target clears `sdkconfig`, so the board's defaults apply. On V1.0 the image is about 0.8 MB of
the 1 MB application partition (about 24 % left, see [code review](code-review.md#flash-size)); V2 uses the
1.5 MB partition of its 8 MB flash and flashes and logs over its USB-C port (`idf.py -p /dev/ttyACM0 flash
monitor`).

### Optional configuration layers

Layers in [`profiles/`](../../profiles) are added on top of `sdkconfig.defaults`:

| Profile | Use it for |
|---|---|
| `bench.defaults` | First power-up, props off: motor ceiling 300 (about 12 % throttle), boots disarmed, failsafe disarms |
| `sysview.defaults` | Debug messages and traces through SEGGER SystemView over JTAG (V1.0: external probe on GPIO12-15, V2: the built-in USB-JTAG) |

```bash
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;profiles/bench.defaults" reconfigure build
```

> When you change `SDKCONFIG_DEFAULTS`, delete the existing `sdkconfig` first, defaults are only read
> when it does not exist.

## 4. Flash and watch the log

```bash
idf.py -p /dev/ttyUSB0 flash monitor      # leave the monitor with Ctrl+]
```

A healthy boot ends with the self-test passing and the system starting. Roughly:

```
SYS: systemStart ! selftestPassed = 1
SYS: Free heap: ... bytes
```

If a module fails its self-test, the log names it (`Self-test of <module> failed`). If the failing module
is not an essential one, the drone waits and can be started anyway by setting the `system.selftestPassed`
parameter from a CRTP client.

## 5. First power-up checklist

Do this with the **propellers removed**.

1. Build with `profiles/bench.defaults`, flash, open the monitor.
2. Check the battery voltage in the log is plausible for the pack (3S 9-12.6 V, 4S 12-16.8 V). The divider
   is 15 k / 82 k on GPIO34.
3. Check the IMU: tilt the board and watch roll/pitch with cfclient or the log.
4. Connect the [reference transmitter](espnow-link.md#writing-a-transmitter). It prints the drone's status.
5. Arm from the transmitter with thrust at zero. Raise thrust slowly, each motor should respond and spin in
   the expected direction. Motor order and direction are not verified on this board yet.
6. Test the failsafe: switch the transmitter off. Motors must stop and the drone must report `FAILSAFE`.
7. Test the kill switch.

## 6. Configure

```bash
idf.py menuconfig          # ESPDrone Config
```

See [Configuration](configuration.md).

## 7. Run the unit tests

```bash
tests/host/run.sh          # needs only gcc, no ESP-IDF
```

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `'cmake' must be available on the PATH` | `idf-env.sh` not sourced in this shell, or the tools were not installed. Run `tools/setup-idf.sh` again. |
| `fatal error: SEGGER_SYSVIEW.h` | SystemView was selected without its IDF options. Use `profiles/sysview.defaults` or pick the ESP-IDF log. |
| `Directory 'build' doesn't seem to be a CMake build directory` | A failed first configure left a partial `build/`. Delete it. |
| `git checkout main` fails with "could be both a local file and a tracking branch" | `main/` is also a directory. Use `git switch main`. |
| Python errors during `install.sh` (`externally-managed-environment`, old packages) | Python 3.12+. `tools/setup-idf.sh` handles it, do not call `install.sh` directly. |
| Build fails in `esp-now` | The managed `espressif/esp-now` component was removed on purpose. Delete `managed_components/` and `dependencies.lock`. |
