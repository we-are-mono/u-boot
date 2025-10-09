#ifndef I2C_HELPERS_H
#define I2C_HELPERS_H

#include <common.h>

struct udevice;

int setup_i2c_muxed_device(int bus, uint8_t mux_addr, uint8_t mux_channel,
                           uint8_t dev_addr, struct udevice **devp);

#endif