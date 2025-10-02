#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <asm/io.h>
#include "i2c_helpers.h"

#define LP5810A_ADDR 0x6C
#define I2C_BUS 2
#define I2C_MUX_ADDR 0x70
#define I2C_MUX_CHANNEL 0x08
#define CHIP_EN_REG 0x00

// LED registers
#define LED_WHITE_REG   0x40
#define LED_BLUE_REG    0x41
#define LED_GREEN_REG   0x42
#define LED_RED_REG     0x43

// LED brightness
#define LED_OFF         0x00
#define LED_FULL        0xFF

int test_lp5810a_led(int any_test_failed)
{
	struct udevice *led_dev;
	uint8_t reg;
	int ret;
	
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL, 
								 LP5810A_ADDR, &led_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "LED controller");
		return ret;
	}
	
	ret = dm_i2c_reg_read(led_dev, CHIP_EN_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read CHIP_EN register)\n", "LED controller");
		return ret;
	}
	
	// Initialize LED controller
	reg = 0x01;
	dm_i2c_write(led_dev, 0x00, &reg, 1);
	reg = 0x00;
	dm_i2c_write(led_dev, 0x02, &reg, 1);
	reg = 0x55;
	dm_i2c_write(led_dev, 0x10, &reg, 1);
	
	// Enable all LEDs
	reg = 0x0f;
	dm_i2c_write(led_dev, 0x20, &reg, 1);
	
	// Set peak current for all LEDs (0x30-0x33)
	reg = 0x0F;
	for (int led = 0x30; led <= 0x33; led++) {
		dm_i2c_write(led_dev, led, &reg, 1);
	}
	
	// Turn off all LEDs first
	reg = LED_OFF;
	dm_i2c_write(led_dev, LED_WHITE_REG, &reg, 1);
	dm_i2c_write(led_dev, LED_BLUE_REG, &reg, 1);
	dm_i2c_write(led_dev, LED_GREEN_REG, &reg, 1);
	dm_i2c_write(led_dev, LED_RED_REG, &reg, 1);
	
	// Turn on appropriate LED
	reg = LED_FULL;
	if (any_test_failed) {
		dm_i2c_write(led_dev, LED_RED_REG, &reg, 1);
	} else {
		dm_i2c_write(led_dev, LED_GREEN_REG, &reg, 1);
	}
	
	printf("%-20s: PASS\n", "LED controller");
	return 0;
}