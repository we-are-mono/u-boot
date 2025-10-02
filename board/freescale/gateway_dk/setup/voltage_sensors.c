#include <common.h>
#include <i2c.h>
#include <dm.h>
#include "i2c_helpers.h"

#define I2C_BUS 2
#define I2C_MUX_ADDR 0x70
#define INA234_BUS_VOLTAGE_REG 0x02
#define VOLTAGE_TOLERANCE_PERCENT 3
#define NUM_SENSORS (sizeof(sensors) / sizeof(sensors[0]))

static int check_voltage_tolerance(uint32_t measured_mv, uint32_t expected_mv, const char *rail_name);

/* Expected voltages for each sensor (in mV, integer) */
typedef struct {
	uint8_t channel;        /* Mux channel (0 or 1) */
	uint8_t address;        /* I2C address */
	uint16_t shunt_mohm;    /* Shunt resistor value in mOhm */
	uint32_t expected_mv;   /* Expected voltage in mV */
	const char *rail_name;  /* Descriptive name */
} sensor_config_t;

static const sensor_config_t sensors[] = {
	/* Channel 0 */
	{0, 0x40, 1, 20000, "20V Power Rail"},
	{0, 0x41, 1,  5000, "5V Power Rail"},
	{0, 0x42, 1,  1000, "1V CPU PSU"},
	{0, 0x43, 5,  1200, "1.2V DDR PSU"},
	
	/* Channel 1 */
	{1, 0x40, 5,  1350, "1.35V SerDes PSU"},
	{1, 0x41, 5,  1800, "1.8V Power Rail"},
	{1, 0x42, 5,  2500, "2.5V Power Rail"},
	{1, 0x43, 1,  3300, "3.3V Power Rail"},
};

static int check_voltage_tolerance(uint32_t measured_mv, uint32_t expected_mv, const char *rail_name)
{
	uint32_t tolerance_mv = (expected_mv * VOLTAGE_TOLERANCE_PERCENT) / 100;
	uint32_t min_mv = expected_mv - tolerance_mv;
	uint32_t max_mv = expected_mv + tolerance_mv;
	
	if (measured_mv < min_mv || measured_mv > max_mv) {
		printf("%-20s: FAIL (%d.%03dV, ±%d%%)\n",
			   rail_name, measured_mv / 1000, measured_mv % 1000, VOLTAGE_TOLERANCE_PERCENT);
		return -1;
	}
	
	printf("%-20s: PASS (%d.%03dV, ±%d%%)\n",
		   rail_name, measured_mv / 1000, measured_mv % 1000, VOLTAGE_TOLERANCE_PERCENT);
	return 0;
}

int test_voltage_sensors(void)
{
	struct udevice *sensor_dev;
	uint8_t buf[2];
	uint16_t raw_voltage;
	uint16_t voltage_bits;
	uint32_t voltage_mv;
	int ret;
	int i;
	int pass_count = 0;
	int fail_count = 0;
	
	/* Test each sensor */
	for (i = 0; i < NUM_SENSORS; i++) {
		const sensor_config_t *sensor = &sensors[i];
		uint8_t mux_channel = (1 << sensor->channel);
		
		/* Setup I2C mux and get sensor device */
		ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, mux_channel,
		                             sensor->address, &sensor_dev);
		if (ret) {
			printf("%-20s: FAIL (Setup failed)\n", sensor->rail_name);
			fail_count++;
			continue;
		}
		
		/* Read bus voltage register */
		ret = dm_i2c_read(sensor_dev, INA234_BUS_VOLTAGE_REG, buf, 2);
		if (ret) {
			printf("%-20s: FAIL (Read failed)\n", sensor->rail_name);
			fail_count++;
			continue;
		}
		
		/* Convert to voltage - INA234: voltage in bits [14:4], LSB = 25.6mV
		 * Using integer math: voltage_mv = (raw >> 4) * 256 / 10
		 * This is equivalent to multiplying by 25.6 */
		raw_voltage = (buf[0] << 8) | buf[1];
		voltage_bits = raw_voltage >> 4;
		voltage_mv = (voltage_bits * 256) / 10;  /* 25.6mV per bit */
		
		/* Check tolerance */
		ret = check_voltage_tolerance(voltage_mv, sensor->expected_mv, sensor->rail_name);
		if (ret == 0) {
			pass_count++;
		} else {
			fail_count++;
		}
	}
	
	return (fail_count == 0) ? 0 : -1;
}