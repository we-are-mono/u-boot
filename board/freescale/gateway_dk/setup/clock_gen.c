#include <common.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define CLOCKGEN_ADDR       0x69
#define I2C_BUS             0
#define I2C_MUX_ADDR        0x70
#define I2C_MUX_CHANNEL     0x01
#define DEVICE_ID_REG       0x07
#define EXPECTED_DEVICE_ID  0x11

int test_6v49205b_clkgen(void)
{
	struct udevice *clkgen_dev;
	uint8_t buf[16];
	int ret;
	
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             CLOCKGEN_ADDR, &clkgen_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "Clock generator");
		return ret;
	}
	
	// Read block of registers starting from 0x00
	ret = dm_i2c_read(clkgen_dev, 0x00, buf, sizeof(buf));
	if (ret) {
		printf("%-20s: FAIL (Failed to read registers)\n", "Clock generator");
		return ret;
	}
	
	// SMBus returns byte count as first byte, so skip it
	// Register 0x07 data is at buf[7+1] = buf[8]
	uint8_t device_id = buf[DEVICE_ID_REG + 1];
	
	if (device_id != EXPECTED_DEVICE_ID) {
		printf("%-20s: FAIL (Device ID mismatch)\n", "Clock generator");
		return -1;
	}
	
	printf("%-20s: PASS\n", "Clock generator");
	return 0;
}