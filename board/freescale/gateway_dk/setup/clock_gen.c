#include <common.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define CLOCKGEN_ADDR       0x69
#define I2C_BUS             0
#define I2C_MUX_ADDR        0x70
#define I2C_MUX_CHANNEL     0x01

/* Register offsets */
#define REG_SLEW_RATE2      0x04
#define REG_REV_VENDOR_ID   0x07

/* Expected values */
#define EXPECTED_DEVICE_ID  0x11  /* Rev 0x1, Vendor 0x1 */

/* Sys_CCB frequency values (bits 3:2 of register 0x04) */
#define CCB_FREQ_66_66      0
#define CCB_FREQ_100        1
#define CCB_FREQ_80         2
#define CCB_FREQ_83_33      3

static const char *get_ccb_freq_string(uint8_t fs_bits)
{
	switch (fs_bits) {
	case CCB_FREQ_66_66:
		return "66.66 MHz";
	case CCB_FREQ_100:
		return "100 MHz";
	case CCB_FREQ_80:
		return "80 MHz";
	case CCB_FREQ_83_33:
		return "83.33 MHz";
	default:
		return "Unknown";
	}
}

int test_6v49205b_clkgen(void)
{
	struct udevice *clkgen_dev;
	uint8_t buf[16];
	uint8_t device_id;
	uint8_t ccb_freq_bits;
	int ret;
	
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             CLOCKGEN_ADDR, &clkgen_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "Clock generator");
		return ret;
	}
	
	ret = dm_i2c_read(clkgen_dev, 0x00, buf, sizeof(buf));
	if (ret) {
		printf("%-20s: FAIL (Failed to read registers)\n", "Clock generator");
		return ret;
	}
	
	/* SMBus returns byte count as first byte, so actual data starts at buf[1] */
	/* Register 0x07 (REV_VENDOR_ID) is at buf[7+1] = buf[8] */
	device_id = buf[REG_REV_VENDOR_ID + 1];
	
	/* Verify device ID */
	if (device_id != EXPECTED_DEVICE_ID) {
		printf("%-20s: FAIL (Device ID 0x%02X, expected 0x%02X)\n", 
		       "Clock generator", device_id, EXPECTED_DEVICE_ID);
		return -1;
	}
	
	/* Read Sys_CCB frequency configuration from byte 4 */
	/* Register 0x04 (SLEW_RATE2) is at buf[4+1] = buf[5] */
	/* Extract FS1:FS0 bits (bits 3:2) */
	ccb_freq_bits = (buf[REG_SLEW_RATE2 + 1] >> 2) & 0x03;
	
	/* Verify frequency is 100 MHz */
	if (ccb_freq_bits != CCB_FREQ_100) {
		printf("%-20s: FAIL (Sys_CCB: %s, expected 100 MHz)\n", 
		       "Clock generator", get_ccb_freq_string(ccb_freq_bits));
		return -1;
	}

	printf("%-20s: PASS (Sys_CCB: 100 MHz)\n", "Clock generator");
	
	return 0;
}