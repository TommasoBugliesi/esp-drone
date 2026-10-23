/* Host stub of the SPI device layer: the test provides a fake register file. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct { int id; } spiDrv;
int spidevInit(spiDrv *dev);
bool spidevRead(spiDrv *dev, uint8_t regAddr, uint8_t *data, size_t length);
bool spidevWrite(spiDrv *dev, uint8_t regAddr, const uint8_t *data, size_t length);
bool spidevReadByte(spiDrv *dev, uint8_t regAddr, uint8_t *data);
bool spidevWriteByte(spiDrv *dev, uint8_t regAddr, uint8_t data);
