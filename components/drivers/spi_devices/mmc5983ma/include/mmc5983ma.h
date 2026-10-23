/*
 * MEMSIC MMC5983MA 3-axis magnetometer on SPI (mode 3, up to 10 MHz).
 *
 * The SET/RESET offset measurement follows ArduPilot's AP_Compass_MMC5xx3.cpp
 * (GPL-3.0-or-later); the continuous mode and the 18-bit data layout follow the
 * MMC5983MA datasheet Rev A. Written for this project on 2026-10-23.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#ifndef MMC5983MA_H_
#define MMC5983MA_H_

#include <stdbool.h>
#include <stdint.h>

#define MMC5983MA_PRODUCT_ID        0x30
#define MMC5983MA_COUNTS_PER_GAUSS  16384.0f   // 18-bit output
#define MMC5983MA_NULL_FIELD        131072     // 18-bit output at zero field
#define MMC5983MA_DATA_LEN          7          // XOUT0 .. XYZOUT2

typedef enum {
    MMC5983MA_RATE_100HZ,    // BW 100 Hz, 0.4 mG rms
    MMC5983MA_RATE_1000HZ,   // BW 800 Hz, 1.2 mG rms
} mmc5983maRate_t;

/**
 * spiDev is an spiDrv *. Resets the chip, measures the bridge offset with one
 * SET and one RESET, then starts continuous measurement with automatic
 * periodic SET. Returns false if the product ID is wrong.
 */
bool mmc5983maInit(void *spiDev, mmc5983maRate_t rate);

/** Latest field in gauss, offset removed. Returns false if no new sample is ready. */
bool mmc5983maRead(float gauss[3]);

bool mmc5983maTest(void);

// Pure helpers, used by the host tests
void mmc5983maDecode(const uint8_t buf[MMC5983MA_DATA_LEN], int32_t counts[3]);
void mmc5983maOffsetFromSetReset(const int32_t set[3], const int32_t reset[3], float offsetCounts[3]);

#endif /* MMC5983MA_H_ */
