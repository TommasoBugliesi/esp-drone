/*
 * Host test of the FlyingFree V2 sensor drivers and the shared sensor processing.
 *
 * The drivers run against fake register files (fake SPI and I2C devices below),
 * so the init sequences, the MMC5983MA SET/RESET offset and the DPS310
 * calibration are exercised without hardware. Built by run.sh.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/task.h"
#include "spidev.h"
#include "i2cdev.h"
#include "icm42688p.h"
#include "mmc5983ma.h"
#include "dps310.h"
#include "sensors_common.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(fabs((double)(a) - (double)(b)) <= (tol))

void vTaskDelay(TickType_t ticks) { (void)ticks; }

/* ------------------------------------------------------------ fake SPI devices */

enum { DEV_IMU = 1, DEV_MAG = 2 };
static spiDrv imuDev = { DEV_IMU };
static spiDrv magDev = { DEV_MAG };

// ICM-42688-P: 5 register banks, bank select at 0x76
static uint8_t imuRegs[5][128];
static uint8_t imuBank;

// MMC5983MA: one register page; a measurement fills XOUT0..XYZOUT2 with the
// SET or RESET reading, depending on the last SET/RESET command
static uint8_t magRegs[64];
static int32_t magSetCounts[3], magResetCounts[3];
static bool magLastWasReset;

static void encode18(const int32_t counts[3], uint8_t buf[7])
{
    memset(buf, 0, 7);
    for (int i = 0; i < 3; i++) {
        const uint32_t v = (uint32_t)(counts[i] + MMC5983MA_NULL_FIELD) & 0x3FFFF;
        buf[2 * i] = v >> 10;
        buf[2 * i + 1] = (v >> 2) & 0xFF;
        buf[6] |= (v & 0x03) << (6 - 2 * i);
    }
}

int spidevInit(spiDrv *dev) { return 1; }

bool spidevRead(spiDrv *dev, uint8_t reg, uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        data[i] = (dev->id == DEV_IMU) ? imuRegs[imuBank][(reg + i) & 0x7F] : magRegs[(reg + i) & 0x3F];
    }
    return true;
}

bool spidevWrite(spiDrv *dev, uint8_t reg, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        const uint8_t r = reg + i, v = data[i];
        if (dev->id == DEV_IMU) {
            if (r == 0x76) {
                imuBank = v & 7;
            } else {
                imuRegs[imuBank][r & 0x7F] = v;
            }
        } else {
            magRegs[r & 0x3F] = v;
            if (r == 0x09 && (v & 0x08)) magLastWasReset = false;   // SET
            if (r == 0x09 && (v & 0x10)) magLastWasReset = true;    // RESET
            if (r == 0x09 && (v & 0x01)) {                          // TM_M: measurement done at once
                encode18(magLastWasReset ? magResetCounts : magSetCounts, &magRegs[0]);
                magRegs[0x08] |= 0x01;
            }
            if (r == 0x08 && (v & 0x01)) magRegs[0x08] &= ~0x01;     // clear Meas_M_Done
        }
    }
    return true;
}

bool spidevReadByte(spiDrv *dev, uint8_t reg, uint8_t *data) { return spidevRead(dev, reg, data, 1); }
bool spidevWriteByte(spiDrv *dev, uint8_t reg, uint8_t data) { return spidevWrite(dev, reg, &data, 1); }

/* ------------------------------------------------------------ fake I2C device (DPS310) */

static I2C_Dev i2cBus;
static uint8_t baroRegs[64];

bool i2cdevReadByte(I2C_Dev *dev, uint8_t addr, uint8_t reg, uint8_t *data) { *data = baroRegs[reg & 0x3F]; return addr == 0x77; }
bool i2cdevReadReg8(I2C_Dev *dev, uint8_t addr, uint8_t reg, uint16_t len, uint8_t *data)
{
    for (uint16_t i = 0; i < len; i++) data[i] = baroRegs[(reg + i) & 0x3F];
    return addr == 0x77;
}
bool i2cdevWriteByte(I2C_Dev *dev, uint8_t addr, uint8_t reg, uint8_t data)
{
    baroRegs[reg & 0x3F] = data;
    if (reg == 0x0C && data == 0x09) baroRegs[0x08] = 0xC0;   // soft reset: coefficients and sensor ready
    return addr == 0x77;
}

/* ------------------------------------------------------------ ICM-42688-P */

static void testIcm42688p(void)
{
    printf("ICM-42688-P\n");
    memset(imuRegs, 0, sizeof(imuRegs));
    imuRegs[0][0x64] = 0x10;                       // INT_CONFIG1 reset value: async reset bit set

    imuRegs[0][0x75] = 0x12;                       // wrong WHO_AM_I
    CHECK(!icm42688pInit(&imuDev, ICM42688P_ODR_1KHZ, ICM42688P_AAF_258HZ));

    imuRegs[0][0x75] = ICM42688P_WHO_AM_I_VALUE;
    CHECK(icm42688pInit(&imuDev, ICM42688P_ODR_1KHZ, ICM42688P_AAF_258HZ));
    CHECK(imuBank == 0);
    CHECK(imuRegs[0][0x4E] == 0x0F);               // gyro and accel in low-noise mode
    CHECK(imuRegs[0][0x4F] == 0x06);               // +-2000 dps, 1 kHz
    CHECK(imuRegs[0][0x50] == 0x06);               // +-16 g, 1 kHz
    CHECK(imuRegs[0][0x14] == 0x03);               // INT1 pulsed, push-pull, active high
    CHECK(imuRegs[0][0x65] == 0x08);               // data ready on INT1
    CHECK((imuRegs[0][0x64] & 0x10) == 0);         // async reset cleared
    CHECK((imuRegs[0][0x4D] & 0xC0) == 0x40);      // AFSR disabled
    CHECK(imuRegs[1][0x0C] == 6 && imuRegs[1][0x0D] == 36 && imuRegs[1][0x0E] == ((10 << 4) | 0));   // AAF 258 Hz
    CHECK(imuRegs[2][0x03] == (6 << 1));

    // TEMP (2), ACCEL X/Y/Z (6), GYRO X/Y/Z (6), big endian
    const uint8_t block[ICM42688P_DATA_LEN] = { 0x01, 0x00, 0x08, 0x00, 0xF8, 0x00, 0x00, 0x01,
                                                0x00, 0x10, 0xFF, 0xF0, 0x7F, 0xFF };
    memcpy(&imuRegs[0][0x1D], block, sizeof(block));
    icm42688pRaw_t raw;
    CHECK(icm42688pReadRaw(&raw));
    CHECK(raw.temp == 256);
    CHECK(raw.acc[0] == 2048 && raw.acc[1] == -2048 && raw.acc[2] == 1);
    CHECK(raw.gyro[0] == 16 && raw.gyro[1] == -16 && raw.gyro[2] == 32767);
    CHECK(icm42688pTest());
}

/* ------------------------------------------------------------ MMC5983MA */

static void testMmc5983ma(void)
{
    printf("MMC5983MA\n");
    // Decode: +1 G on X, -1 count on Y, full scale on Z
    const int32_t in[3] = { 16384, -1, 131071 };
    uint8_t buf[7];
    int32_t out[3];
    encode18(in, buf);
    mmc5983maDecode(buf, out);
    CHECK(out[0] == in[0] && out[1] == in[1] && out[2] == in[2]);

    // Offset from SET/RESET: offset 1000 counts, field 4096 counts (0.25 G) on each axis
    memset(magRegs, 0, sizeof(magRegs));
    for (int i = 0; i < 3; i++) {
        magSetCounts[i] = 1000 + 4096;
        magResetCounts[i] = 1000 - 4096;
    }
    CHECK(!mmc5983maInit(&magDev, MMC5983MA_RATE_100HZ));    // product ID still 0
    magRegs[0x2F] = MMC5983MA_PRODUCT_ID;
    CHECK(mmc5983maInit(&magDev, MMC5983MA_RATE_100HZ));
    CHECK(magRegs[0x0A] == 0x00);                             // BW 100 Hz
    CHECK(magRegs[0x0B] == (0x80 | 0x30 | 0x08 | 0x05));      // periodic SET, continuous, 100 Hz

    float gauss[3];
    CHECK(!mmc5983maRead(gauss));                             // no new sample yet
    const int32_t sample[3] = { 1000 + 8192, 1000, 1000 - 16384 };
    encode18(sample, &magRegs[0]);
    magRegs[0x08] |= 0x01;
    CHECK(mmc5983maRead(gauss));
    CHECK_NEAR(gauss[0], 0.5, 1e-6);
    CHECK_NEAR(gauss[1], 0.0, 1e-6);
    CHECK_NEAR(gauss[2], -1.0, 1e-6);
    CHECK((magRegs[0x08] & 0x01) == 0);                       // done flag cleared
    CHECK(!mmc5983maRead(gauss));

    CHECK(mmc5983maInit(&magDev, MMC5983MA_RATE_1000HZ));
    CHECK(magRegs[0x0A] == 0x03);                             // BW 800 Hz
    CHECK((magRegs[0x0B] & 0x07) == 0x07);                    // 1000 Hz
}

/* ------------------------------------------------------------ DPS310 */

static void put16(uint8_t *p, int32_t v) { p[0] = (v >> 8) & 0xFF; p[1] = v & 0xFF; }

static void testDps310(void)
{
    printf("DPS310\n");
    const dps310Coef_t ref = { .c0 = 50, .c1 = -264, .c00 = 80000, .c10 = -50000, .c01 = -2000,
                               .c11 = 1500, .c20 = -10000, .c21 = 200, .c30 = -1000 };
    uint8_t c[DPS310_COEF_LEN];
    const uint32_t c0 = ref.c0 & 0xFFF, c1 = ref.c1 & 0xFFF, c00 = ref.c00 & 0xFFFFF, c10 = ref.c10 & 0xFFFFF;
    c[0] = c0 >> 4;
    c[1] = ((c0 & 0x0F) << 4) | (c1 >> 8);
    c[2] = c1 & 0xFF;
    c[3] = c00 >> 12;
    c[4] = (c00 >> 4) & 0xFF;
    c[5] = ((c00 & 0x0F) << 4) | (c10 >> 16);
    c[6] = (c10 >> 8) & 0xFF;
    c[7] = c10 & 0xFF;
    put16(&c[8], ref.c01);
    put16(&c[10], ref.c11);
    put16(&c[12], ref.c20);
    put16(&c[14], ref.c21);
    put16(&c[16], ref.c30);

    dps310Coef_t parsed;
    dps310ParseCoef(c, &parsed);
    CHECK(memcmp(&parsed, &ref, sizeof(ref)) == 0);

    float p, t;
    dps310Compensate(&ref, 0, 0, &p, &t);                     // raw 0: P = c00, T = c0 / 2
    CHECK_NEAR(p, 80000, 1e-3);
    CHECK_NEAR(t, 25.0, 1e-6);
    dps310Compensate(&ref, 253952, 0, &p, &t);                // scaled P = 1: c00 + c10 + c20 + c30
    CHECK_NEAR(p, 80000 - 50000 - 10000 - 1000, 1e-2);

    CHECK(dps310TwosComplement(0xFFFFFF, 24) == -1);
    CHECK(dps310TwosComplement(0x7FFFFF, 24) == 0x7FFFFF);
    CHECK_NEAR(dps310PressureToAltitude(1013.25f), 0.0, 1e-3);
    CHECK_NEAR(dps310PressureToAltitude(900.0f), 988.5, 1.0);

    // Through the driver with the fake chip
    memset(baroRegs, 0, sizeof(baroRegs));
    baroRegs[0x0D] = DPS310_PRODUCT_ID;
    memcpy(&baroRegs[0x10], c, sizeof(c));
    CHECK(dps310Init(&i2cBus, DPS310_I2C_ADDR_SDO_HIGH));
    CHECK(baroRegs[0x06] == 0x54 && (baroRegs[0x07] & 0x7F) == 0x54);
    CHECK(baroRegs[0x09] == 0x0C && baroRegs[0x08] == 0x07);
    memset(&baroRegs[0x00], 0, 6);                            // raw P = T = 0
    float mbar, degC, asl;
    CHECK(dps310Read(&mbar, &degC, &asl));
    CHECK_NEAR(mbar, 800.0, 1e-3);
    CHECK_NEAR(degC, 25.0, 1e-5);
    CHECK_NEAR(asl, dps310PressureToAltitude(800.0f), 1e-3);
}

/* ------------------------------------------------------------ sensors_common */

static sensorsBias_t bias;

static void testSensorsCommon(void)
{
    printf("sensors_common\n");
    sensorsBiasInit(&bias, 8000, 1000);
    bool found = false;
    for (int i = 0; i < SENSORS_NBR_OF_BIAS_SAMPLES - 1; i++) {
        found = sensorsBiasUpdate(&bias, 10 + (i & 1), -20, 5, 2000);
    }
    CHECK(!found);                                            // buffer not full yet
    found = sensorsBiasUpdate(&bias, 11, -20, 5, 2000);
    CHECK(found);
    CHECK_NEAR(bias.bias.x, 10.5, 1e-3);
    CHECK_NEAR(bias.bias.y, -20, 1e-3);
    CHECK_NEAR(bias.bias.z, 5, 1e-3);

    // A moving vehicle (large variance) never gives a bias
    sensorsBiasInit(&bias, 8000, 1000);
    for (int i = 0; i < 2 * SENSORS_NBR_OF_BIAS_SAMPLES; i++) {
        found = sensorsBiasUpdate(&bias, (i & 1) ? 1000 : -1000, 0, 0, 2000);
    }
    CHECK(!found);

    sensorsAccScale_t scale;
    sensorsAccScaleInit(&scale);
    for (int i = 0; i < SENSORS_ACC_SCALE_SAMPLES; i++) {
        sensorsAccScaleUpdate(&scale, 1.02f);
    }
    CHECK(scale.isFound);
    CHECK_NEAR(scale.scale, 1.02, 1e-5);

    const float flip[3][3] = { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 } };
    const Axis3f v = { .x = 1, .y = 2, .z = 3 };
    Axis3f r;
    sensorsRotate(flip, &v, &r);
    CHECK(r.x == 1 && r.y == -2 && r.z == -3);

    sensorsAlign_t align;
    sensorsAlignInit(&align, 0, 0);
    sensorsAlignToGravity(&align, &v, &r);
    CHECK_NEAR(r.x, 1, 1e-6);
    CHECK_NEAR(r.y, 2, 1e-6);
    CHECK_NEAR(r.z, 3, 1e-6);
    sensorsAlignInit(&align, 0, 90);                          // roll 90 deg: y goes to z
    const Axis3f yOnly = { .x = 0, .y = 1, .z = 0 };
    sensorsAlignToGravity(&align, &yOnly, &r);
    CHECK_NEAR(r.z, 1, 1e-6);
    CHECK_NEAR(r.y, 0, 1e-6);
}

int main(void)
{
    testIcm42688p();
    testMmc5983ma();
    testDps310();
    testSensorsCommon();
    if (failures) {
        printf("test_sensor_drivers: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_sensor_drivers: all passed\n");
    return 0;
}
