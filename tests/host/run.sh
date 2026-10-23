#!/bin/sh
# Builds and runs the host tests with the system gcc (no ESP-IDF needed).
set -e
cd "$(dirname "$0")"
out=${TMPDIR:-/tmp}/espdrone-host-tests
mkdir -p "$out"
gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
    -include stubs/test_config.h -Istubs -I../../components/core/crazyflie/hal/interface \
    test_espnow_link.c -o "$out/test_espnow_link" -lm
"$out/test_espnow_link"

C=../../components
gcc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
    -Istubs_drivers -I$C/drivers/spi_devices/icm42688p/include -I$C/drivers/spi_devices/mmc5983ma/include \
    -I$C/drivers/i2c_devices/dps310/include -I$C/core/crazyflie/hal/interface -I$C/core/crazyflie/utils/interface \
    test_sensor_drivers.c $C/drivers/spi_devices/icm42688p/icm42688p.c $C/drivers/spi_devices/mmc5983ma/mmc5983ma.c \
    $C/drivers/i2c_devices/dps310/dps310.c $C/core/crazyflie/hal/src/sensors_common.c \
    $C/core/crazyflie/utils/src/filter.c -o "$out/test_sensor_drivers" -lm
"$out/test_sensor_drivers"
