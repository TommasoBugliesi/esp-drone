#ifndef SPI_BUS_H
#define SPI_BUS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "sdkconfig.h"

#include "stm32_legacy.h"

// One SPI bus (host and its three signal pins)
typedef struct {
    spi_host_device_t host;
    int sclk_pin;
    int mosi_pin;
    int miso_pin;
    uint32_t max_transfer_size;
} spiBusDef;

// One device on a bus
typedef struct {
    const spiBusDef *bus;
    int cs_pin;
    uint32_t speed;                  // clock in Hz
    uint8_t mode;                    // SPI mode 0-3
} spiDef;

typedef struct {
    const spiDef *def;                    //< Definition of the device
    SemaphoreHandle_t isBusFreeMutex;     //< Mutex of the bus, shared by its devices
    spi_device_handle_t handle;           //< Handle of this device
} spiDrv;

// Devices of the selected board, defined in spi_drv.c
extern spiDrv imuSpiDev;
#ifdef CONFIG_TARGET_FLYINGFREE_V02
extern spiDrv magSpiDev;
extern spiDrv osdSpiDev;
#endif

// Initializes the device's bus (once) and adds the device to it
void spidrvInit(spiDrv *spi);

#endif // SPI_BUS_H
