#include <common.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define DS100DF410_ADDR 0x18
#define I2C_BUS 0
#define I2C_MUX_ADDR 0x70
#define I2C_MUX_CHANNEL 0x01
#define DEVICE_ID_REG 0x01

// Expected device ID value
#define EXPECTED_DEVICE_ID 0xD0

int test_ds100df410_retimer(void)
{
	struct udevice *retimer_dev;
	uint8_t device_id;
	int ret;
	
	// Setup I2C mux and get retimer device
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             DS100DF410_ADDR, &retimer_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "Retimer");
		return ret;
	}
	
	// Read device ID register
	ret = dm_i2c_reg_read(retimer_dev, DEVICE_ID_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read device ID)\n", "Retimer");
		return ret;
	}
	device_id = (uint8_t)ret;
	
	// Verify device ID matches expected value
	if (device_id != EXPECTED_DEVICE_ID) {
		printf("%-20s: FAIL (Device ID mismatch)\n", "Retimer");
		return -1;
	}
	
	printf("%-20s: PASS\n", "Retimer");
	return 0;
}