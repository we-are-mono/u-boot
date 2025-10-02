// board/freescale/gateway_dk/setup/fan_controller_test.c

#include <common.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define EMC2302_ADDR        0x2E
#define I2C_BUS             0
#define I2C_MUX_ADDR        0x70
#define I2C_MUX_CHANNEL     0x08
#define TACH_HIGH_REG       0x3E
#define TACH_LOW_REG        0x3F

// Valid tachometer range. The number 3932160 comes from the datasheet.
// RPM = (3932160 * 2) / tach_reading
// For 300 RPM minimum: tach_reading must be < 26214 (0x6666)
#define TACH_MAX_FOR_300RPM 0x6666

static int fan_controller_init(struct udevice *fan_dev)
{
	uint8_t reg;
	int ret;
	
	// Invert polarity and set output type to push-pull
	reg = 0xff;
	ret = dm_i2c_write(fan_dev, 0x2a, &reg, 1);
	if (ret)
		return ret;
	ret = dm_i2c_write(fan_dev, 0x2b, &reg, 1);
	if (ret)
		return ret;
	
	// Set fans to fixed 35% speed, Linux will take care of proper speeds
	reg = 0x5A;
	ret = dm_i2c_write(fan_dev, 0x30, &reg, 1);
	if (ret)
		return ret;
	ret = dm_i2c_write(fan_dev, 0x40, &reg, 1);
	if (ret)
		return ret;
	
	return 0;
}

int test_emc2302_fan(void)
{
	struct udevice *fan_dev;
	uint16_t tach_reading;
	int ret;
	
	// Setup I2C mux and get fan controller device
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             EMC2302_ADDR, &fan_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "Fan controller");
		return ret;
	}
	
	// Initialize fan controller
	ret = fan_controller_init(fan_dev);
	if (ret) {
		printf("%-20s: FAIL (Initialization failed)\n", "Fan controller");
		return ret;
	}
	
	// Read tachometer high byte
	ret = dm_i2c_reg_read(fan_dev, TACH_HIGH_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read tach high byte)\n", "Fan controller");
		return ret;
	}
	tach_reading = ((uint8_t)ret) << 8;
	
	// Read tachometer low byte
	ret = dm_i2c_reg_read(fan_dev, TACH_LOW_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read tach low byte)\n", "Fan controller");
		return ret;
	}
	tach_reading |= (uint8_t)ret;
	
	// Check if fan is spinning fast enough
	if (tach_reading > TACH_MAX_FOR_300RPM) {
		printf("%-20s: FAIL (Fan too slow or not detected)\n", "Fan controller");
		return -1;
	}
	
	// Calculate and display RPM
	uint32_t rpm = (3932160 * 2) / tach_reading;
	
	printf("%-20s: PASS (%u RPM, Fan 1)\n", "Fan controller", rpm);
	return 0;
}