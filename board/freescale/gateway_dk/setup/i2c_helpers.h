#ifndef __I2C_HELPERS_H__
#define __I2C_HELPERS_H__

#include <dm.h>

int setup_i2c_muxed_device(int bus, uint8_t mux_addr, uint8_t channel, 
						   uint8_t dev_addr, struct udevice **dev);

#endif