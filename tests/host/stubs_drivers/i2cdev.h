/* Host stub of the I2C device layer: the test provides a fake register file. */
#include <stdint.h>
#include <stdbool.h>
typedef struct { int id; } I2C_Dev;
bool i2cdevReadByte(I2C_Dev *dev, uint8_t devAddress, uint8_t memAddress, uint8_t *data);
bool i2cdevReadReg8(I2C_Dev *dev, uint8_t devAddress, uint8_t memAddress, uint16_t len, uint8_t *data);
bool i2cdevWriteByte(I2C_Dev *dev, uint8_t devAddress, uint8_t memAddress, uint8_t data);
