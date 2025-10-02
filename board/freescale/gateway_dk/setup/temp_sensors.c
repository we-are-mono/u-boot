#include <common.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define TMP431_ADDR         0x4C
#define I2C_BUS             0
#define I2C_MUX_ADDR        0x70
#define CPU_TEMP_CHANNEL    0x02
#define BOARD_TEMP_CHANNEL  0x04
#define REMOTE_TEMP_REG     0x01
#define LOCAL_TEMP_REG      0x00

// Temperature range limits (in degrees Celsius)
#define TEMP_MIN            15
#define TEMP_MAX            60

int test_tmp431_temperatures(void)
{
	struct udevice *temp_dev;
	int ret;
	int cpu_temp, board_temp;
	
	// Read CPU temperature (remote sensor on channel 0x02)
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, CPU_TEMP_CHANNEL,
	                             TMP431_ADDR, &temp_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "CPU temperature");
		return ret;
	}
	
	ret = dm_i2c_reg_read(temp_dev, REMOTE_TEMP_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read CPU temperature)\n", "CPU temperature");
		return ret;
	}
	cpu_temp = (int)(uint8_t)ret;
	
	// Verify CPU temperature is in valid range
	if (cpu_temp < TEMP_MIN || cpu_temp > TEMP_MAX) {
		printf("%-20s: FAIL (Out of range: %d°C)\n", "CPU temperature", cpu_temp);
		return -1;
	}
	
	printf("%-20s: PASS (%d°C)\n", "CPU temperature", cpu_temp);
	
	// Read board temperature (local sensor on channel 0x04)
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, BOARD_TEMP_CHANNEL,
	                             TMP431_ADDR, &temp_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "Board temperature");
		return ret;
	}
	
	ret = dm_i2c_reg_read(temp_dev, LOCAL_TEMP_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Failed to read board temperature)\n", "Board temperature");
		return ret;
	}
	board_temp = (int)(uint8_t)ret;
	
	// Verify board temperature is in valid range
	if (board_temp < TEMP_MIN || board_temp > TEMP_MAX) {
		printf("%-20s: FAIL (Out of range: %d°C)\n", "Board temperature", board_temp);
		return -1;
	}
	
	printf("%-20s: PASS (%d°C)\n", "Board temperature", board_temp);
	
	return 0;
}