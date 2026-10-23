/**
 * ESP-Drone Firmware
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * sensors_flyingfree_v02.c - Sensors of the FlyingFree V2 (ESP32-S3).
 *
 * The ICM-42688-P runs at 1 kHz and raises INT1 for every sample. The
 * interrupt wakes the sensors task, which reads the IMU, the magnetometer
 * when it has a new sample, and the barometer at 25 Hz, then releases the
 * stabilizer. Processing (bias, scale, alignment, filters) is in
 * sensors_common.c. Same interface as sensors_flyingfree_v01.c.
 */

#include <math.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "queue.h"
#include "driver/gpio.h"
#include "esp_attr.h"

#include "sensors_flyingfree_v02.h"
#include "sensors_common.h"
#include "system.h"
#include "ledseq.h"
#include "sound.h"
#include "usec_time.h"
#include "config.h"
#include "stm32_legacy.h"
#include "static_mem.h"

#include "spidev.h"
#include "i2cdev.h"
#include "icm42688p.h"
#include "mmc5983ma.h"
#include "dps310.h"

#define DEBUG_MODULE "SENSORS"
#include "debug_cf.h"

#define SENSORS_RATE_HZ             1000
#define GYRO_VARIANCE_BASE          8000      // raw units, same 16.4 LSB/(deg/s) scale as the BMI160 setting
#define GYRO_MIN_BIAS_TIMEOUT_MS    M2T(1 * 1000)
#define GYRO_LPF_CUTOFF_FREQ        80
#define ACCEL_LPF_CUTOFF_FREQ       30
#define BARO_DIVIDER                (SENSORS_RATE_HZ / 25)
#define DATA_READY_TIMEOUT_MS       10        // no IMU interrupt for this long: report it

#define SENSORS_DEG_PER_LSB         (1.0f / ICM42688P_GYRO_LSB_PER_DPS)
#define SENSORS_G_PER_LSB           (1.0f / ICM42688P_ACC_LSB_PER_G)

#define PITCH_CALIB (CONFIG_PITCH_CALIB * 1.0f / 100)
#define ROLL_CALIB  (CONFIG_ROLL_CALIB * 1.0f / 100)

/*
 * Sensor axes to body axes (x forward, y left, z up). Identity until the PCB
 * layout fixes the orientation of the ICM-42688-P and the MMC5983MA: update
 * both matrices then, and check them on the bench (tilt and turn the board).
 */
#ifdef CONFIG_UPSIDEDOWN_ENABLE
    #define FLIP -1.0f
#else
    #define FLIP 1.0f
#endif
static const float imuToBody[3][3] = {
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, FLIP, 0.0f },
    { 0.0f, 0.0f, FLIP },
};
static const float magToBody[3][3] = {
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, FLIP, 0.0f },
    { 0.0f, 0.0f, FLIP },
};

static xQueueHandle accelerometerDataQueue;
STATIC_MEM_QUEUE_ALLOC(accelerometerDataQueue, 1, sizeof(Axis3f));
static xQueueHandle gyroDataQueue;
STATIC_MEM_QUEUE_ALLOC(gyroDataQueue, 1, sizeof(Axis3f));
static xQueueHandle magnetometerDataQueue;
STATIC_MEM_QUEUE_ALLOC(magnetometerDataQueue, 1, sizeof(Axis3f));
static xQueueHandle barometerDataQueue;
STATIC_MEM_QUEUE_ALLOC(barometerDataQueue, 1, sizeof(baro_t));

static xSemaphoreHandle imuDataReady;
static StaticSemaphore_t imuDataReadyBuffer;
static xSemaphoreHandle dataReady;
static StaticSemaphore_t dataReadyBuffer;
STATIC_MEM_TASK_ALLOC(sensorsTask, SENSORS_TASK_STACKSIZE);

static bool isInit = false;
static bool isImuPresent = false;
static bool isMagnetometerPresent = false;
static bool isBarometerPresent = false;

static sensorData_t sensorData;
static sensorsBias_t gyroBias;
static sensorsAccScale_t accScale;
static sensorsAlign_t accAlign;
static lpf2pData gyroLpf[3];
static lpf2pData accLpf[3];
static uint32_t missedInterrupts;

static void IRAM_ATTR imuInterruptHandler(void *arg)
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(imuDataReady, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void sensorsInterruptInit(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CONFIG_IMU_INT1_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,    // push-pull output, the pull only holds it while the IMU is off
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&io);
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {   // already installed is fine
        DEBUG_PRINTE("GPIO ISR service: error %d", err);
        return;
    }
    gpio_isr_handler_add(CONFIG_IMU_INT1_PIN, imuInterruptHandler, NULL);
}

static void sensorsDeviceInit(void)
{
    i2cdevInit(I2C0_DEV);

    isImuPresent = icm42688pInit(IMU_SPI_DEV, ICM42688P_ODR_1KHZ, ICM42688P_AAF_258HZ);
    if (!isImuPresent) {
        DEBUG_PRINTE("ICM-42688-P [FAIL]");
    }

#ifdef CONFIG_MMC5983MA_ENABLE
#ifdef CONFIG_MMC5983MA_RATE_1000HZ
    isMagnetometerPresent = mmc5983maInit(MAG_SPI_DEV, MMC5983MA_RATE_1000HZ);
#else
    isMagnetometerPresent = mmc5983maInit(MAG_SPI_DEV, MMC5983MA_RATE_100HZ);
#endif
    DEBUG_PRINTI("MMC5983MA [%s]", isMagnetometerPresent ? "OK" : "FAIL");
#endif

#ifdef CONFIG_DPS310_ENABLE
    isBarometerPresent = dps310Init(I2C0_DEV, DPS310_I2C_ADDR_SDO_HIGH);
    DEBUG_PRINTI("DPS310 [%s]", isBarometerPresent ? "OK" : "FAIL");
#endif

    sensorsLpfInit(gyroLpf, SENSORS_RATE_HZ, GYRO_LPF_CUTOFF_FREQ);
    sensorsLpfInit(accLpf, SENSORS_RATE_HZ, ACCEL_LPF_CUTOFF_FREQ);
    sensorsAlignInit(&accAlign, PITCH_CALIB, ROLL_CALIB);
    DEBUG_PRINTI("pitch_calib = %f, roll_calib = %f", PITCH_CALIB, ROLL_CALIB);
}

static void processImu(const icm42688pRaw_t *raw)
{
    const bool wasCalibrated = gyroBias.isBiasValueFound;
    const bool biasFound = sensorsBiasUpdate(&gyroBias, raw->gyro[0], raw->gyro[1], raw->gyro[2],
                                             xTaskGetTickCount());
    if (biasFound && !wasCalibrated) {
        soundSetEffect(SND_CALIB);
        ledseqRun(&seq_calibrated);
        DEBUG_PRINTI("gyro bias found");
    }

    Axis3f acc = {
        .x = raw->acc[0] * SENSORS_G_PER_LSB,
        .y = raw->acc[1] * SENSORS_G_PER_LSB,
        .z = raw->acc[2] * SENSORS_G_PER_LSB,
    };
    if (biasFound) {
        sensorsAccScaleUpdate(&accScale, sqrtf(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z));
    }
    Axis3f gyro = {
        .x = (raw->gyro[0] - gyroBias.bias.x) * SENSORS_DEG_PER_LSB,
        .y = (raw->gyro[1] - gyroBias.bias.y) * SENSORS_DEG_PER_LSB,
        .z = (raw->gyro[2] - gyroBias.bias.z) * SENSORS_DEG_PER_LSB,
    };
    for (int i = 0; i < 3; i++) {
        acc.axis[i] /= accScale.scale;
    }

    sensorsRotate(imuToBody, &gyro, &sensorData.gyro);
    sensorsLpfApply(gyroLpf, &sensorData.gyro);

    Axis3f accBody;
    sensorsRotate(imuToBody, &acc, &accBody);
    sensorsAlignToGravity(&accAlign, &accBody, &sensorData.acc);
    sensorsLpfApply(accLpf, &sensorData.acc);
}

static void sensorsTask(void *param)
{
    uint32_t sampleCount = 0;

    systemWaitStart();
    vTaskDelay(M2T(200));
    DEBUG_PRINTD("sensorsTask started");

    while (1) {
        if (xSemaphoreTake(imuDataReady, M2T(DATA_READY_TIMEOUT_MS)) != pdTRUE) {
            if (missedInterrupts++ % 100 == 0) {
                DEBUG_PRINTW("no IMU data-ready interrupt (%u)", (unsigned)missedInterrupts);
            }
            continue;
        }
        sensorData.interruptTimestamp = usecTimestamp();
        sampleCount++;

        icm42688pRaw_t raw;
        if (icm42688pReadRaw(&raw)) {
            processImu(&raw);
            xQueueOverwrite(accelerometerDataQueue, &sensorData.acc);
            xQueueOverwrite(gyroDataQueue, &sensorData.gyro);
        }

        float gauss[3];
        if (isMagnetometerPresent && mmc5983maRead(gauss)) {
            const Axis3f mag = { .x = gauss[0], .y = gauss[1], .z = gauss[2] };
            sensorsRotate(magToBody, &mag, &sensorData.mag);
            xQueueOverwrite(magnetometerDataQueue, &sensorData.mag);
        }

        if (isBarometerPresent && (sampleCount % BARO_DIVIDER) == 0) {
            if (dps310Read(&sensorData.baro.pressure, &sensorData.baro.temperature, &sensorData.baro.asl)) {
                xQueueOverwrite(barometerDataQueue, &sensorData.baro);
            }
        }

        xSemaphoreGive(dataReady);
    }
}

void sensorsFF02Init(void)
{
    if (isInit) {
        return;
    }

    sensorsBiasInit(&gyroBias, GYRO_VARIANCE_BASE, GYRO_MIN_BIAS_TIMEOUT_MS);
    sensorsAccScaleInit(&accScale);

    imuDataReady = xSemaphoreCreateBinaryStatic(&imuDataReadyBuffer);
    dataReady = xSemaphoreCreateBinaryStatic(&dataReadyBuffer);

    sensorsDeviceInit();

    accelerometerDataQueue = STATIC_MEM_QUEUE_CREATE(accelerometerDataQueue);
    gyroDataQueue = STATIC_MEM_QUEUE_CREATE(gyroDataQueue);
    magnetometerDataQueue = STATIC_MEM_QUEUE_CREATE(magnetometerDataQueue);
    barometerDataQueue = STATIC_MEM_QUEUE_CREATE(barometerDataQueue);

    if (isImuPresent) {
        sensorsInterruptInit();
    }
    STATIC_MEM_TASK_CREATE(sensorsTask, sensorsTask, SENSORS_TASK_NAME, NULL, SENSORS_TASK_PRI);

    isInit = true;
}

bool sensorsFF02Test(void)
{
    bool pass = isInit && icm42688pTest();
#ifdef CONFIG_MMC5983MA_ENABLE
    pass = pass && mmc5983maTest();
#endif
    // The barometer is optional for flight: report only
    if (isBarometerPresent && !dps310Test()) {
        DEBUG_PRINTW("DPS310 test failed");
    }
    return pass;
}

bool sensorsFF02AreCalibrated(void)
{
    return gyroBias.isBiasValueFound;
}

void sensorsFF02Acquire(sensorData_t *sensors, const uint32_t tick)
{
    sensorsFF02ReadGyro(&sensors->gyro);
    sensorsFF02ReadAcc(&sensors->acc);
    sensorsFF02ReadMag(&sensors->mag);
    sensorsFF02ReadBaro(&sensors->baro);
    sensors->interruptTimestamp = sensorData.interruptTimestamp;
}

void sensorsFF02WaitDataReady(void)
{
    xSemaphoreTake(dataReady, portMAX_DELAY);
}

bool sensorsFF02ReadGyro(Axis3f *gyro)
{
    return (pdTRUE == xQueueReceive(gyroDataQueue, gyro, 0));
}

bool sensorsFF02ReadAcc(Axis3f *acc)
{
    return (pdTRUE == xQueueReceive(accelerometerDataQueue, acc, 0));
}

bool sensorsFF02ReadMag(Axis3f *mag)
{
    return (pdTRUE == xQueueReceive(magnetometerDataQueue, mag, 0));
}

bool sensorsFF02ReadBaro(baro_t *baro)
{
    return (pdTRUE == xQueueReceive(barometerDataQueue, baro, 0));
}

void sensorsFF02SetAccMode(accModes accMode)
{
}
