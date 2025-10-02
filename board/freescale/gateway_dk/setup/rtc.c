#include <common.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define PCF2131_ADDR 0x53
#define I2C_BUS 2
#define I2C_MUX_ADDR 0x70
#define I2C_MUX_CHANNEL 0x04
#define CONTROL_2_REG 0x02

// PWRMNG[2:0] should be set to 000 (battery switch-over enabled)
#define PWRMNG_MASK 0xE0
#define PWRMNG_EXPECTED 0x00

int test_pcf2131_rtc(void)
{
	struct udevice *rtc_dev;
	uint8_t control_2;
	int ret;
	
	// Setup I2C mux and get RTC device
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             PCF2131_ADDR, &rtc_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "RTC");
		return ret;
	}
	
	// Read Control_2 register
	ret = dm_i2c_reg_read(rtc_dev, CONTROL_2_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read Control_2 register)\n", "RTC");
		return ret;
	}
	control_2 = (uint8_t)ret;
	
	// Check if PWRMNG[2:0] needs to be configured
	if ((control_2 & PWRMNG_MASK) != PWRMNG_EXPECTED) {
		// Clear PWRMNG bits to enable battery switch-over
		control_2 = control_2 & ~PWRMNG_MASK;
		
		ret = dm_i2c_reg_write(rtc_dev, CONTROL_2_REG, control_2);
		if (ret) {
			printf("%-20s: FAIL (Failed to configure PWRMNG)\n", "RTC");
			return ret;
		}
	}
	
	printf("%-20s: PASS\n", "RTC");
	return 0;
}