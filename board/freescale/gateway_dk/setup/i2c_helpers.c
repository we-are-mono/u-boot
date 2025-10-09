#include <common.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

int setup_i2c_muxed_device(int bus, uint8_t mux_addr, uint8_t channel, 
                           uint8_t dev_addr, struct udevice **dev)
{
    struct udevice *mux_dev;
    int ret;
    
    ret = i2c_get_chip_for_busnum(bus, mux_addr, 1, &mux_dev);
    if (ret)
        return ret;
    
    ret = dm_i2c_write(mux_dev, 0x00, &channel, 1);
    if (ret)
        return ret;
    
    return i2c_get_chip_for_busnum(bus, dev_addr, 1, dev);
}