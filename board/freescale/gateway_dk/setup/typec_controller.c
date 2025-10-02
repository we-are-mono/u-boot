#include <common.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define HD3SS3220_ADDR 0x47
#define I2C_BUS 2
#define I2C_MUX_ADDR 0x70
#define I2C_MUX_CHANNEL 0x04

// Expected device ID: "TUSB322" in ASCII
// Registers 0x00-0x07 contain: {0x00, 0x54, 0x55, 0x53, 0x42, 0x33, 0x32, 0x32}, but are read in reverse order
static const uint8_t expected_device_id[8] = {0x32, 0x32, 0x33, 0x42, 0x53, 0x55, 0x54, 0x00};

int test_hd3ss3220(void)
{
	struct udevice *hd3ss_dev;
	uint8_t device_id[8];
	int ret;
	int i;
	int mismatch = 0;
	
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             HD3SS3220_ADDR, &hd3ss_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "USB Type-C ctrl.");
		return ret;
	}
	
	ret = dm_i2c_read(hd3ss_dev, 0x00, device_id, 8);
	if (ret) {
		printf("%-20s: FAIL (Failed to read device ID)\n", "USB Type-C ctrl.");
		return ret;
	}
	
	for (i = 0; i < 8; i++) {
		if (device_id[i] != expected_device_id[i]) {
			mismatch = 1;
			break;
		}
	}
	
	if (mismatch) {
		printf("%-20s: FAIL (Device ID mismatch)\n", "USB Type-C ctrl.");
		return -1;
	}
	
	// Print device ID in reverse order to show ASCII string
	printf("%-20s: PASS (", "USB Type-C ctrl.");
	for (i = 7; i >= 0; i--) {
		if (device_id[i] >= 0x20 && device_id[i] <= 0x7E) {
			printf("%c", device_id[i]);
		}
	}
	printf(")\n");
	return 0;
}