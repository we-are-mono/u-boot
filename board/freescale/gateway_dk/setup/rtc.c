#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <rtc.h>
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
	struct udevice *i2c_dev;
	struct udevice *rtc_dev;
	uint8_t control_2;
	struct rtc_time tm;
	int ret;
	
	// Setup I2C mux and get I2C device
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             PCF2131_ADDR, &i2c_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "RTC");
		return ret;
	}
	
	// Read Control_2 register using I2C device
	ret = dm_i2c_reg_read(i2c_dev, CONTROL_2_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read Control_2 register)\n", "RTC");
		return ret;
	}
	control_2 = (uint8_t)ret;
	
	// Check if PWRMNG[2:0] needs to be configured
	if ((control_2 & PWRMNG_MASK) != PWRMNG_EXPECTED) {
		// Clear PWRMNG bits to enable battery switch-over
		control_2 = control_2 & ~PWRMNG_MASK;
		
		ret = dm_i2c_reg_write(i2c_dev, CONTROL_2_REG, control_2);
		if (ret) {
			printf("%-20s: FAIL (Failed to configure PWRMNG)\n", "RTC");
			return ret;
		}
	}
	
	// Now find the RTC device by name for dm_rtc_get()
	ret = uclass_get_device_by_name(UCLASS_RTC, "rtc@53", &rtc_dev);
	if (ret) {
		printf("%-20s: FAIL (RTC device not found)\n", "RTC");
		return ret;
	}
	
	// Read time using RTC subsystem
	ret = dm_rtc_get(rtc_dev, &tm);
	if (ret) {
		printf("%-20s: FAIL (Failed to read time)\n", "RTC");
		return ret;
	}
	
	printf("%-20s: PASS (%04d-%02d-%02d %02d:%02d:%02d)\n", 
	       "RTC", tm.tm_year, tm.tm_mon, tm.tm_mday, 
	       tm.tm_hour, tm.tm_min, tm.tm_sec);
	
	return 0;
}