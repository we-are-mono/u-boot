#include <config.h>
#include <linux/delay.h>
#include <stdio.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define EMC2302_ADDR        0x2E
#define I2C_BUS             0
#define I2C_MUX_ADDR        0x70
#define I2C_MUX_CHANNEL     0x08
#define TACH_HIGH_REG       0x3E
#define TACH_LOW_REG        0x3F
#define FAN_STALL_STATUS    0x25

/* Valid tachometer range. The number 3932160 comes from the datasheet. */
/* EMC2305_TACH_CNT_MULTIPLIER = 2 */
#define EMC230X_RPM_FACTOR  3932160U
#define TACH_MULTIPLIER     2U
#define MIN_RPM             1000

static int fan_controller_init(struct udevice *fan_dev)
{
	uint8_t reg;
	int ret;

	/* Invert polarity and set output type to push-pull */
	reg = 0xff;
	ret = dm_i2c_write(fan_dev, 0x2a, &reg, 1);
	if (ret)
		return ret;
	ret = dm_i2c_write(fan_dev, 0x2b, &reg, 1);
	if (ret)
		return ret;

	/* Set fans to fixed 50% speed, Linux will take care of proper speeds */
	reg = 0x80;
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
	uint32_t tach_raw, tach_shifted;
	uint32_t base, rpm;
	int ret;

	/* Setup I2C mux and get fan controller device */
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             EMC2302_ADDR, &fan_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "Fan controller");
		return ret;
	}

	/* Initialize fan controller */
	ret = fan_controller_init(fan_dev);
	if (ret) {
		printf("%-20s: FAIL (Initialization failed)\n", "Fan controller");
		return ret;
	}

	/* Give the fan ample amount of time to spin up */
	mdelay(1000);

	/* Read tachometer high byte */
	ret = dm_i2c_reg_read(fan_dev, TACH_HIGH_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read tach high byte)\n", "Fan controller");
		return ret;
	}
	tach_raw = ((uint8_t)ret) << 8;

	/* Read tachometer low byte */
	ret = dm_i2c_reg_read(fan_dev, TACH_LOW_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read tach low byte)\n", "Fan controller");
		return ret;
	}
	tach_raw |= (uint8_t)ret;

	/* Check for stalled fan (tach reading 0xFFF0 or similar) */
	if (tach_raw >= 0xFFF0) {
		printf("%-20s: FAIL (Fan stalled, tach=0x%04x)\n", "Fan controller", tach_raw);
		return -1;
	}

	/* Right-shift by 3 bits per datasheet (tach count is left-aligned) */
	tach_shifted = tach_raw >> 3;

	/* Calculate RPM */
	base = EMC230X_RPM_FACTOR / tach_shifted;
	rpm = base * TACH_MULTIPLIER;

	/* Check if fan is spinning fast enough (minimum 2000 RPM at 50% speed) */
	if (rpm < MIN_RPM) {
		printf("%-20s: FAIL (Fan too slow: %u RPM)\n", "Fan controller", rpm);
		return -1;
	}

	printf("%-20s: PASS (%u RPM, Fan 1)\n", "Fan controller", rpm);
	return 0;
}