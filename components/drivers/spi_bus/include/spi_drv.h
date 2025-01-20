#ifndef SPI_BUS_H
#define SPI_BUS_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"

#include "stm32_legacy.h"

// SPI Bus configuration structure
typedef struct {
    spi_host_device_t spiPort;      // SPI Port (HSPI_HOST or VSPI_HOST)
    uint32_t sclk_pin;               // Clock pin
    uint32_t mosi_pin;               // MOSI pin
    uint32_t miso_pin;               // MISO pin
    uint32_t cs_pin;                 // Chip Select pin
    uint32_t speed;                   // Transfer speed
    uint32_t max_transfer_size;      // Maximum transfer size
} spiDef;

typedef struct {
    const spiDef *def;                    //< Definition of the spi
    SemaphoreHandle_t isBusFreeMutex;     //< Mutex to protect bus
    spi_device_handle_t handle;
} spiDrv;

// Function to initialize the SPI bus
void spidrvInit(spiDrv *spi);

void spidrvAddDevice(spiDrv *spi);

#endif // SPI_BUS_H