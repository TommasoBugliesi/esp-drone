/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * ESP-Drone Firmware
 *
 * Copyright 2019-2020  Espressif Systems (Shanghai)
 * Copyright (C) 2011-2018 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Implements HAL for sensors MPU9250 and LPS25H
 *
 * 2016.06.15: Initial version by Mike Hamer, http://mikehamer.info
 */

/* Include section start*/
#include <math.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "queue.h"
#include "projdefs.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "sensors_flyingfree_v01.h"
#include "system.h"
#include "configblock.h"
#include "param.h"
#include "log.h"

#include "imu.h"
#include "nvicconf.h"
#include "ledseq.h"
#include "sound.h"
#include "filter.h"
#include "config.h"
#include "stm32_legacy.h"

#include "spidev.h"
#include "i2cdev.h"
#include "bmi160.h"
#include "qmc5883l.h"

#define DEBUG_MODULE "SENSORS"
#include "debug_cf.h"
#include "static_mem.h"
#include "crtp_commander.h"
/* Include section end*/

/* Macros section start*/

// processAccGyroMeasurement
#define GYRO_NBR_OF_AXES 3
#define SENSORS_NBR_OF_BIAS_SAMPLES 1024
#define GYRO_VARIANCE_BASE 8000 // If bmi parameters are updated a new value may be required
#define GYRO_MIN_BIAS_TIMEOUT_MS M2T(1 * 1000)
#define GYRO_VARIANCE_THRESHOLD_X (GYRO_VARIANCE_BASE)
#define GYRO_VARIANCE_THRESHOLD_Y (GYRO_VARIANCE_BASE)
#define GYRO_VARIANCE_THRESHOLD_Z (GYRO_VARIANCE_BASE)
#define SENSORS_ACC_SCALE_SAMPLES 200

#define BMI160_ACCEL_RANGE BMI160_ACCEL_RANGE_8G
#if BMI160_ACCEL_RANGE == BMI160_ACCEL_RANGE_2G
    #define SENSORS_G_PER_LSB_CFG      (float)((2 * 2) / 65536.0)
#elif BMI160_ACCEL_RANGE == BMI160_ACCEL_RANGE_4G
    #define SENSORS_G_PER_LSB_CFG      (float)((2 * 4) / 65536.0)
#elif BMI160_ACCEL_RANGE == BMI160_ACCEL_RANGE_8G
    #define SENSORS_G_PER_LSB_CFG      (float)((2 * 8) / 65536.0)
#elif BMI160_ACCEL_RANGE == BMI160_ACCEL_RANGE_16G
    #define SENSORS_G_PER_LSB_CFG     (float)((2 * 16) / 65536.0)
#endif

#define BMI160_GYRO_RANGE BMI160_GYRO_RANGE_2000_DPS
#if BMI160_GYRO_RANGE == BMI160_GYRO_RANGE_250_DPS
    #define SENSORS_DEG_PER_LSB_CFG  (float)((2 * 250.0) / 65536.0)
#elif BMI160_GYRO_RANGE == BMI160_GYRO_RANGE_500_DPS
    #define SENSORS_DEG_PER_LSB_CFG  (float)((2 * 500.0) / 65536.0)
#elif BMI160_GYRO_RANGE == BMI160_GYRO_RANGE_1000_DPS
    #define SENSORS_DEG_PER_LSB_CFG (float)((2 * 1000.0) / 65536.0)
#elif BMI160_GYRO_RANGE == BMI160_GYRO_RANGE_2000_DPS
    #define SENSORS_DEG_PER_LSB_CFG (float)((2 * 2000.0) / 65536.0)
#endif

#define QMC5885L_MAG_RANGE QMC5883L_RNG_2
#if QMC5885L_MAG_RANGE == QMC5883L_RNG_2
    #define MAG_GAUSS_PER_LSB (float)((2 * 2.0) / 65536.0)
#elif QMC5885L_MAG_RANGE == QMC5883L_RNG_8
    #define MAG_GAUSS_PER_LSB (float)((2 * 8.0) / 65536.0)
#endif

// #define PITCH_CALIB (CONFIG_PITCH_CALIB*1.0/100)
// #define ROLL_CALIB (CONFIG_ROLL_CALIB*1.0/100)
#define PITCH_CALIB (-168.0f*1.0/100)
#define ROLL_CALIB (300.0f*1.0/100)

#ifdef CONFIG_UPSIDEDOWN_ENABLE
    #define UPSIDEDOWN -1.0f // Z axis depends on board installation 
#endif
#ifndef CONFIG_UPSIDEDOWN_ENABLE
    #define UPSIDEDOWN 1.0f
#endif

#ifdef CONFIG_TARGET_FLYINGFREE_V01
    #define IMU_X_TOFRAME -1.0f // Sensors axis must be brought back to MPU axis of S2_Drone_V1_2
    #define IMU_Y_TOFRAME -1.0f
    #define MAG_X_TOFRAME 1.0f
    #define MAG_Y_TOFRAME 1.0f
#endif

// #define DEBUG_EP2 1
/* Macros section end*/

/* Datatypes section start*/

/* Datatypes section end*/

/* Variables section start*/
typedef struct {
    Axis3f bias;
    Axis3f variance;
    Axis3f mean;
    bool isBiasValueFound;
    bool isBufferFilled;
    Axis3i16 *bufHead;
    Axis3i16 buffer[SENSORS_NBR_OF_BIAS_SAMPLES];
} BiasObj;

// Pre-calculated values for accelerometer alignment
float cosPitch;
float sinPitch;
float cosRoll;
float sinRoll;

static xQueueHandle accelerometerDataQueue;
STATIC_MEM_QUEUE_ALLOC(accelerometerDataQueue, 1, sizeof(Axis3f));
static xQueueHandle gyroDataQueue;
STATIC_MEM_QUEUE_ALLOC(gyroDataQueue, 1, sizeof(Axis3f));
static xQueueHandle magnetometerDataQueue;
STATIC_MEM_QUEUE_ALLOC(magnetometerDataQueue, 1, sizeof(Axis3f));
static xQueueHandle barometerDataQueue;
STATIC_MEM_QUEUE_ALLOC(barometerDataQueue, 1, sizeof(baro_t));

TimerHandle_t xsensorDataReadyTimer;
static xSemaphoreHandle sensorsDataReady;
static StaticSemaphore_t sensorsDataReadyBuffer;
static xSemaphoreHandle dataReady;
static StaticSemaphore_t dataReadyBuffer;
STATIC_MEM_TASK_ALLOC(sensorsTask, SENSORS_TASK_STACKSIZE);

#ifdef CONFIG_BMP280_ENABLE
    static bool isBarometerPresent = true;
#else
    static bool isBarometerPresent = false;
#endif
#ifdef CONFIG_QMC5883L_ENABLE
    static bool isMagnetometerPresent = true;
#else
    static bool isMagnetometerPresent = false;
#endif

static bool isInit = false;

static sensorData_t sensorData;
static struct bmi160_dev bmi160dev;
bool mag_data_ready = false;

const TickType_t sensorReadIntervalMs = pdMS_TO_TICKS(1);  

// processAccGyroMeasurement
    static Axis3i16 gyroRaw;
    static Axis3i16 accelRaw;
    static BiasObj gyroBiasRunning;
    static Axis3f gyroBias;
    static bool gyroBiasFound = false;
    static float accScaleSum = 0;
    static float accScale = 1;

// Low Pass filtering
    #define GYRO_LPF_CUTOFF_FREQ 80
    #define ACCEL_LPF_CUTOFF_FREQ 30
    static lpf2pData accLpf[3];
    static lpf2pData gyroLpf[3];
    static void applyAxis3fLpf(lpf2pData *data, Axis3f *in);

/* Variables section end*/

/* Functions section start*/
static void processAccGyroMeasurements(struct bmi160_sensor_data *bmi160_accel, struct bmi160_sensor_data *bmi160_gyro);
static void processMagnetometerMeasurements(struct qmc5883l_raw_data_t *qmc5883l_mag);
// static void processBarometerMeasurements(const uint8_t *buffer);
static bool processGyroBias(int16_t gx, int16_t gy, int16_t gz, Axis3f *gyroBiasOut);
static bool processAccScale(int16_t ax, int16_t ay, int16_t az);
static void sensorsBiasObjInit(BiasObj *bias);
static void sensorsCalculateVarianceAndMean(BiasObj *bias, Axis3f *varOut, Axis3f *meanOut);
static void sensorsAddBiasValue(BiasObj *bias, int16_t x, int16_t y, int16_t z);
static bool sensorsFindBiasValue(BiasObj *bias);
static void sensorsAccAlignToGravity(Axis3f *in, Axis3f *out);
static void timerCallback(TimerHandle_t xTimer);


/* BMI160 spi functions to integrate sensor library */
int8_t bmi160_spi_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *data, uint16_t length);
int8_t bmi160_spi_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *data, uint16_t length);

// SPI Read Write function for BMI160
int8_t bmi160_spi_read(uint8_t dev_id, uint8_t reg_addr, uint8_t *data, uint16_t length) {
    if (spidevRead(VSPI_DEV, reg_addr, data, length)) {
        return BMI160_OK;
    }
    return BMI160_E_COM_FAIL;
}

// Write function for BMI160
int8_t bmi160_spi_write(uint8_t dev_id, uint8_t reg_addr, uint8_t *data, uint16_t length) {
    if (spidevWrite(VSPI_DEV, reg_addr, data, length)) {
        return BMI160_OK;
    }
    return BMI160_E_COM_FAIL;
}

static void sensorsDeviceInit(void)
{
    // Wait for sensors to startup
    while (xTaskGetTickCount() < 2000){
        vTaskDelay(M2T(50));
    };

    int8_t rslt;
    i2cdevInit(I2C0_DEV);
    spidevInit(VSPI_DEV);

    // Assign the SPI device pointer to the dev_id field
    bmi160dev.id = (uint8_t)VSPI_DEV.def->spiPort; // Cast the address of spiDevice as uint8_t (Bosch uses this as a handle)
    bmi160dev.intf = BMI160_SPI_INTF; // Set interface type to SPI
    bmi160dev.read = bmi160_spi_read;       // Set read function
    bmi160dev.write = bmi160_spi_write;     // Set write function
    bmi160dev.delay_ms = vTaskDelay;        // Delay function (uses FreeRTOS)

    // Init the bmi160 and then set its options
    rslt = bmi160_init(&bmi160dev);

    if (rslt == BMI160_OK)
    {
        DEBUG_PRINTI("BMI160 initialization success !\n");
        DEBUG_PRINTI("Chip ID 0x%X\n", bmi160dev.chip_id);
    }
    else
    {
        DEBUG_PRINTE("BMI160 initialization failure !\n");
        assert(0); // Terminate the program
    }

    bmi160_soft_reset(&bmi160dev);
    vTaskDelay(M2T(50));

    // Select the Output data rate, range of accelerometer sensor 
    bmi160dev.accel_cfg.odr = BMI160_ACCEL_ODR_1600HZ;
    bmi160dev.accel_cfg.range = BMI160_ACCEL_RANGE;
    bmi160dev.accel_cfg.bw = BMI160_ACCEL_BW_OSR2_AVG2;

    // Select the power mode of accelerometer sensor 
    bmi160dev.accel_cfg.power = BMI160_ACCEL_NORMAL_MODE;

    // Select the Output data rate, range of Gyroscope sensor 
    bmi160dev.gyro_cfg.odr = BMI160_GYRO_ODR_1600HZ;
    bmi160dev.gyro_cfg.range = BMI160_GYRO_RANGE;
    bmi160dev.gyro_cfg.bw = BMI160_GYRO_BW_OSR4_MODE;

    // Select the power mode of Gyroscope sensor 
    bmi160dev.gyro_cfg.power = BMI160_GYRO_NORMAL_MODE;

    // Set the sensor configuration 
    rslt = bmi160_set_sens_conf(&bmi160dev);

    // Integrate digital LPF
    for (uint8_t i = 0; i < 3; i++) {
        lpf2pInit(&gyroLpf[i], 1000, GYRO_LPF_CUTOFF_FREQ);
        lpf2pInit(&accLpf[i], 1000, ACCEL_LPF_CUTOFF_FREQ);
    }


#ifdef CONFIG_QMC5883L_ENABLE
    qmc5883lInit(I2C0_DEV);
    
    if (qmc5883lGetID() == QMC5883L_CHIP_ID) {
        isMagnetometerPresent = true;
        qmc5883lSetReg1(QMC5883L_OSR_256, QMC5885L_MAG_RANGE, QMC5883L_ODR_200, QMC5883L_MODE_CONTINUOUS); 
        DEBUG_PRINTI("qmc5883l I2C connection [OK].\n");
    } else {
        DEBUG_PRINTW("qmc5883l I2C connection [FAIL].\n");
    }

#endif
#ifdef CONFIG_BMP280_ENABLE
    ms5611Init(I2C0_DEV);

    if (false) {
        isBarometerPresent = true;
        DEBUG_PRINTI("MS5611 I2C connection [OK].\n");
    } else {
        //TODO: Should sensor test fail hard if no connection
        DEBUG_PRINTW("MS5611 I2C connection [FAIL].\n");
    }

#endif

#ifdef SENSORS_ENABLE_RANGE_VL53L1X
    zRanger2Init();

    if (zRanger2Test() == true) {
        isVl53l1xPresent = true;
        DEBUG_PRINTI("VL53L1X I2C connection [OK].\n");
    } else {
        //TODO: Should sensor test fail hard if no connection
        DEBUG_PRINTW("VL53L1X I2C connection [FAIL].\n");
    }

#endif

    DEBUG_PRINTI("sensors init done");


    cosPitch = cosf(PITCH_CALIB * (float)M_PI / 180);
    sinPitch = sinf(PITCH_CALIB * (float)M_PI / 180);
    cosRoll = cosf(ROLL_CALIB * (float)M_PI / 180);
    sinRoll = sinf(ROLL_CALIB * (float)M_PI / 180);
    DEBUG_PRINTI("pitch_calib = %f,roll_calib = %f",PITCH_CALIB,ROLL_CALIB);
}

static void timerCallback(TimerHandle_t xTimer){
    xSemaphoreGive(sensorsDataReady); // Give the semaphore
}
static void sensorsTask(void *param)
{
    //Previous software TODO present. to be clarified
    systemWaitStart();
    vTaskDelay(M2T(200));
    xsensorDataReadyTimer = xTimerCreate("sensorDataReady", pdMS_TO_TICKS(1), pdTRUE, NULL, timerCallback);

    // Check if the timer was created successfully
    if (xsensorDataReadyTimer == NULL) {
        printf("Failed to create sensorDataReady timer!\n");
        return;
    }

    // Start the timer
    if (xTimerStart(xsensorDataReadyTimer, 0) != pdPASS) {
        printf("Failed to start timer!\n");
    }

    DEBUG_PRINTD("xTaskCreate sensorsTask IN");
    
    while (1) {
        if (pdTRUE == xSemaphoreTake(sensorsDataReady, portMAX_DELAY)){

        /* sensors step 1 - read data */
        struct bmi160_sensor_data bmi160_accel;
        struct bmi160_sensor_data bmi160_gyro;
        struct qmc5883l_raw_data_t qmc5883l_mag;

        bmi160_get_sensor_data((BMI160_ACCEL_SEL | BMI160_GYRO_SEL), &bmi160_accel, &bmi160_gyro, &bmi160dev);
        
        if (isMagnetometerPresent){
            if (qmc5883lGetReadyStatus()){
                qmc5883lGetHeading(&qmc5883l_mag);
                mag_data_ready = true;
            }
        }

        /* sensors step 2 - process the respective data */
        processAccGyroMeasurements(&bmi160_accel, &bmi160_gyro);

        if (isMagnetometerPresent) {
            processMagnetometerMeasurements(&qmc5883l_mag);
        }

        // if (isBarometerPresent) {
        //     processBarometerMeasurements(&(buffer[isMagnetometerPresent ? SENSORS_MPU6050_BUFF_LEN + SENSORS_MAG_BUFF_LEN : SENSORS_MPU6050_BUFF_LEN]));
        // }

        /* sensors step 3 - queue sensors data on the output queues */
        xQueueOverwrite(accelerometerDataQueue, &sensorData.acc);
        xQueueOverwrite(gyroDataQueue, &sensorData.gyro);

        if (isMagnetometerPresent) {
            xQueueOverwrite(magnetometerDataQueue, &sensorData.mag);
        }

        if (isBarometerPresent) {
            xQueueOverwrite(barometerDataQueue, &sensorData.baro);
        }

        #ifdef DEBUG_EP2
            DEBUG_PRINT_LOCAL("ax = %f,  ay = %f,  az = %f,  gx = %f,  gy = %f,  gz = %f , hx = %f , hy = %f, hz =%f \n", 
                            sensorData.acc.x, sensorData.acc.y, sensorData.acc.z, 
                            sensorData.gyro.x, sensorData.gyro.y, sensorData.gyro.z, 
                            sensorData.mag.x, sensorData.mag.y, sensorData.mag.z);
        #endif

        /* sensors step 4 - Unlock stabilizer task */
        xSemaphoreGive(dataReady);
        }
    }
}

static void sensorsTaskInit(void)
{
  accelerometerDataQueue = STATIC_MEM_QUEUE_CREATE(accelerometerDataQueue);
  gyroDataQueue = STATIC_MEM_QUEUE_CREATE(gyroDataQueue);
  magnetometerDataQueue = STATIC_MEM_QUEUE_CREATE(magnetometerDataQueue);
  barometerDataQueue = STATIC_MEM_QUEUE_CREATE(barometerDataQueue);

  STATIC_MEM_TASK_CREATE(sensorsTask, sensorsTask, SENSORS_TASK_NAME, NULL, SENSORS_TASK_PRI);
  DEBUG_PRINTD("xTaskCreate sensorsTask \n");
}

void processAccGyroMeasurements(struct bmi160_sensor_data *bmi160_accel, struct bmi160_sensor_data *bmi160_gyro){
    /*  Note the ordering to correct the rotated 90º IMU coordinate system */

    Axis3f accScaled;

    /* sensors step 2.1 read raw data */
    accelRaw.y = bmi160_accel->x;
    accelRaw.x = bmi160_accel->y;
    accelRaw.z = bmi160_accel->z;
    gyroRaw.y = bmi160_gyro->x;
    gyroRaw.x = bmi160_gyro->y;
    gyroRaw.z = bmi160_gyro->z;

    /* sensors step 2.2 Calculates the gyro bias first when the  variance is below threshold */
    gyroBiasFound = processGyroBias(gyroRaw.x, gyroRaw.y, gyroRaw.z, &gyroBias);

    /*sensors step 2.3 Calculates the acc scale when platform is steady */
    if (gyroBiasFound) {
        processAccScale(accelRaw.x, accelRaw.y, accelRaw.z);
    }

    /* sensors step 2.35 convert from sensor reference frame to drone reference frame */
    // TODO : convert both data and offsets

    /* sensors step 2.4 convert  digtal value to physical angle */
    sensorData.gyro.y = IMU_X_TOFRAME               * ((gyroRaw.x - gyroBias.x) * SENSORS_DEG_PER_LSB_CFG);
    sensorData.gyro.x = IMU_Y_TOFRAME * UPSIDEDOWN  * ((gyroRaw.y - gyroBias.y) * SENSORS_DEG_PER_LSB_CFG);
    sensorData.gyro.z = UPSIDEDOWN                  * ((gyroRaw.z - gyroBias.z) * SENSORS_DEG_PER_LSB_CFG);
    accScaled.y       = IMU_X_TOFRAME               * ((accelRaw.x)             * SENSORS_G_PER_LSB_CFG / accScale);   
    accScaled.x       = IMU_Y_TOFRAME * UPSIDEDOWN  * ((accelRaw.y)             * SENSORS_G_PER_LSB_CFG / accScale);
    accScaled.z       = UPSIDEDOWN                  * ((accelRaw.z)             * SENSORS_G_PER_LSB_CFG / accScale);

    /* sensors step 2.5 low pass filter */
    applyAxis3fLpf((lpf2pData *)(&gyroLpf), &sensorData.gyro);

    /* sensors step 2.6 Compensate for a miss-aligned accelerometer. */
    sensorsAccAlignToGravity(&accScaled, &sensorData.acc);
    applyAxis3fLpf((lpf2pData *)(&accLpf), &sensorData.acc);
}

/**
 * Calculates the bias first when the gyro variance is below threshold. Requires a buffer
 * but calibrates platform first when it is stable.
 */
static bool processGyroBias(int16_t gx, int16_t gy, int16_t gz, Axis3f *gyroBiasOut)
{
    sensorsAddBiasValue(&gyroBiasRunning, gx, gy, gz);

    if (!gyroBiasRunning.isBiasValueFound) {
        sensorsFindBiasValue(&gyroBiasRunning);

        if (gyroBiasRunning.isBiasValueFound) {
            //TODO:
            soundSetEffect(SND_CALIB);
            ledseqRun(&seq_calibrated);
            DEBUG_PRINTI("isBiasValueFound!");
        }
    }

    gyroBiasOut->x = gyroBiasRunning.bias.x;
    gyroBiasOut->y = gyroBiasRunning.bias.y;
    gyroBiasOut->z = gyroBiasRunning.bias.z;

    return gyroBiasRunning.isBiasValueFound;
}

/**
 * Adds a new value to the variance buffer and if it is full
 * replaces the oldest one. Thus a circular buffer.
 */
static void sensorsAddBiasValue(BiasObj *bias, int16_t x, int16_t y, int16_t z)
{
    bias->bufHead->x = x;
    bias->bufHead->y = y;
    bias->bufHead->z = z;
    bias->bufHead++;
    
    // When the buffer is full, link the buffer pointer to its head
    if (bias->bufHead >= &bias->buffer[SENSORS_NBR_OF_BIAS_SAMPLES]) {
        bias->bufHead = bias->buffer;
        bias->isBufferFilled = true;
    }
}

/**
 * Checks if the variances is below the predefined thresholds.
 * The bias value should have been added before calling this.
 * @param bias  The bias object
 */
static bool sensorsFindBiasValue(BiasObj *bias)
{
    static int32_t varianceSampleTime;
    bool foundBias = false;

    if (bias->isBufferFilled) {
        sensorsCalculateVarianceAndMean(bias, &bias->variance, &bias->mean);

        if (bias->variance.x < GYRO_VARIANCE_THRESHOLD_X &&
            bias->variance.y < GYRO_VARIANCE_THRESHOLD_Y &&
            bias->variance.z < GYRO_VARIANCE_THRESHOLD_Z &&
            (varianceSampleTime + GYRO_MIN_BIAS_TIMEOUT_MS < xTaskGetTickCount())) 
            {
            varianceSampleTime = xTaskGetTickCount();
            bias->bias.x = bias->mean.x;
            bias->bias.y = bias->mean.y;
            bias->bias.z = bias->mean.z;
            foundBias = true;
            bias->isBiasValueFound = true;
        }
    }

    return foundBias;
}

/**
 * Calculates the variance and mean for the bias buffer.
 */
static void sensorsCalculateVarianceAndMean(BiasObj *bias, Axis3f *varOut, Axis3f *meanOut)
{
    uint32_t i;
    int64_t sum[GYRO_NBR_OF_AXES] = {0};
    int64_t sumSq[GYRO_NBR_OF_AXES] = {0};

    for (i = 0; i < SENSORS_NBR_OF_BIAS_SAMPLES; i++) {
        sum[0] += bias->buffer[i].x;
        sum[1] += bias->buffer[i].y;
        sum[2] += bias->buffer[i].z;
        sumSq[0] += bias->buffer[i].x * bias->buffer[i].x;
        sumSq[1] += bias->buffer[i].y * bias->buffer[i].y;
        sumSq[2] += bias->buffer[i].z * bias->buffer[i].z;
    }

    varOut->x = (sumSq[0] - ((int64_t)sum[0] * sum[0]) / SENSORS_NBR_OF_BIAS_SAMPLES);
    varOut->y = (sumSq[1] - ((int64_t)sum[1] * sum[1]) / SENSORS_NBR_OF_BIAS_SAMPLES);
    varOut->z = (sumSq[2] - ((int64_t)sum[2] * sum[2]) / SENSORS_NBR_OF_BIAS_SAMPLES);

    meanOut->x = (float)sum[0] / SENSORS_NBR_OF_BIAS_SAMPLES;
    meanOut->y = (float)sum[1] / SENSORS_NBR_OF_BIAS_SAMPLES;
    meanOut->z = (float)sum[2] / SENSORS_NBR_OF_BIAS_SAMPLES;

    // DEBUG_PRINT_LOCAL("Variance X: %f, Y: %f, Z: %f\n", varOut->x, varOut->y, varOut->z);
    // DEBUG_PRINT_LOCAL("Mean X: %f, Y: %f, Z: %f\n", meanOut->x, meanOut->y, meanOut->z);
}


/**
 * Calculates accelerometer scale out of SENSORS_ACC_SCALE_SAMPLES samples. Should be called when
 * platform is stable.
 */
static bool processAccScale(int16_t ax, int16_t ay, int16_t az)
{
    static bool accBiasFound = false;
    static uint32_t accScaleSumCount = 0;

    if (!accBiasFound) {
        accScaleSum += sqrtf(powf(ax * SENSORS_G_PER_LSB_CFG, 2) + powf(ay * SENSORS_G_PER_LSB_CFG, 2) + powf(az * SENSORS_G_PER_LSB_CFG, 2));
        accScaleSumCount++;

        if (accScaleSumCount == SENSORS_ACC_SCALE_SAMPLES) {
            accScale = accScaleSum / SENSORS_ACC_SCALE_SAMPLES;
            accBiasFound = true;
        }
    }

    return accBiasFound;
}


/**
 * Compensate for a miss-aligned accelerometer. It uses the trim
 * data gathered from the UI and written in the config-block to
 * rotate the accelerometer to be aligned with gravity.
 */
static void sensorsAccAlignToGravity(Axis3f *in, Axis3f *out)
{
    Axis3f rx;
    Axis3f ry;

    // Rotate around x-axis
    rx.x = in->x;
    rx.y = in->y * cosRoll - in->z * sinRoll;
    rx.z = in->y * sinRoll + in->z * cosRoll;

    // Rotate around y-axis
    ry.x = rx.x * cosPitch - rx.z * sinPitch;
    ry.y = rx.y;
    ry.z = -rx.x * sinPitch + rx.z * cosPitch;

    out->x = ry.x;
    out->y = ry.y;
    out->z = ry.z;
}

void processMagnetometerMeasurements(struct qmc5883l_raw_data_t *qmc5883l_mag)
{
    if (mag_data_ready) {
        sensorData.mag.y = MAG_X_TOFRAME *              ((float)qmc5883l_mag->x / MAG_GAUSS_PER_LSB); //to gauss
        sensorData.mag.x = MAG_Y_TOFRAME * UPSIDEDOWN * ((float)qmc5883l_mag->y / MAG_GAUSS_PER_LSB);
        sensorData.mag.z = UPSIDEDOWN *                 ((float)qmc5883l_mag->z / MAG_GAUSS_PER_LSB);

        mag_data_ready = false;
        // DEBUG_PRINTI("hmc5883l DATA ready");
    } else {

        // DEBUG_PRINTW("hmc5883l DATA not ready");
    }
}

static void applyAxis3fLpf(lpf2pData *data, Axis3f *in)
{
    for (uint8_t i = 0; i < 3; i++) {
        in->axis[i] = lpf2pApply(&data[i], in->axis[i]);
    }
}

void sensorsFF01Acquire(sensorData_t *sensors, const uint32_t tick)
{
    sensorsReadGyro(&sensors->gyro);
    sensorsReadAcc(&sensors->acc);
    sensorsReadMag(&sensors->mag);
    sensorsReadBaro(&sensors->baro);
}

bool sensorsFF01ReadGyro(Axis3f *gyro)
{
    return (pdTRUE == xQueueReceive(gyroDataQueue, gyro, 0));
}

void sensorsFF01WaitDataReady(void)
{
    xSemaphoreTake(dataReady, portMAX_DELAY);
}


bool sensorsFF01ReadAcc(Axis3f *acc)
{
    return (pdTRUE == xQueueReceive(accelerometerDataQueue, acc, 0));
}

bool sensorsFF01ReadMag(Axis3f *mag)
{
    return (pdTRUE == xQueueReceive(magnetometerDataQueue, mag, 0));
}

bool sensorsFF01ReadBaro(baro_t *baro)
{
    return (pdTRUE == xQueueReceive(barometerDataQueue, baro, 0));
}

static void sensorsBiasObjInit(BiasObj *bias)
{
    bias->isBufferFilled = false;
    bias->bufHead = bias->buffer;
}

void sensorsFF01Init(void)
{
    if (isInit) {
        return;
    }
    sensorsBiasObjInit(&gyroBiasRunning);
    sensorsDeviceInit();

    sensorsDataReady = xSemaphoreCreateBinaryStatic(&sensorsDataReadyBuffer);
    dataReady = xSemaphoreCreateBinaryStatic(&dataReadyBuffer);

    sensorsTaskInit();
    isInit = true;
}

bool sensorsFF01AreCalibrated()
{
    return gyroBiasFound;
}
/* Functions section end */