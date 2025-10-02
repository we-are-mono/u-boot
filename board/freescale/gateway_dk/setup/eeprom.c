#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <command.h>
#include <exports.h>
#include <asm/io.h>

#define EEPROM_ADDR 0x50
#define I2C_BUS 3
#define MAGIC_OFFSET 0x00
#define MAGIC_NUMBER 0x4D414743  // "MAGC" in ASCII

#define GPIO3_BASE 0x02320000
#define GPIO3_GPDIR 0x00
#define GPIO3_GPDAT 0x08
#define GPIO3_PIN0_MASK 0x80000000  // Bit 31 in first byte position

int test_eeprom(void)
{
	struct udevice *eeprom_dev;
	uint8_t magic_bytes[4];
	uint8_t read_back[4];
	int ret;
	int i;
	
	// Magic number in big-endian byte order
	magic_bytes[0] = 0x4D;  // 'M'
	magic_bytes[1] = 0x41;  // 'A'
	magic_bytes[2] = 0x47;  // 'G'
	magic_bytes[3] = 0x43;  // 'C'
	
	// Unlock EEPROM
	// Read current values and modify only the needed bits
	u32 gpio_dir = in_be32((void *)(GPIO3_BASE + GPIO3_GPDIR));
	u32 gpio_dat = in_be32((void *)(GPIO3_BASE + GPIO3_GPDAT));
	
	// Set GPIO3_00 as output and drive it low
	out_be32((void *)(GPIO3_BASE + GPIO3_GPDIR), gpio_dir | 0x80000000);
	out_be32((void *)(GPIO3_BASE + GPIO3_GPDAT), gpio_dat & ~0x80000000);
	
	// Read back to ensure write completed
	(void)in_be32((void *)(GPIO3_BASE + GPIO3_GPDAT));
	
	udelay(10000);  // Wait 10ms for EEPROM to unlock
	
	// Get EEPROM device with 2-byte address offset
	ret = i2c_get_chip_for_busnum(I2C_BUS, EEPROM_ADDR, 2, &eeprom_dev);
	if (ret) {
		printf("%-20s: FAIL (Device not found at 0x%02x)\n", "EEPROM", EEPROM_ADDR);
		return ret;
	}
	
	// Read current magic number
	ret = dm_i2c_read(eeprom_dev, MAGIC_OFFSET, read_back, 4);
	if (ret) {
		printf("%-20s: FAIL (Failed to read)\n", "EEPROM");
		return ret;
	}
	
	// Check if magic number is already written
	int magic_valid = 1;
	for (i = 0; i < 4; i++) {
		if (read_back[i] != magic_bytes[i]) {
			magic_valid = 0;
			break;
		}
	}
	
	if (!magic_valid) {
		// Write magic number
		ret = dm_i2c_write(eeprom_dev, MAGIC_OFFSET, magic_bytes, 4);
		if (ret) {
			printf("%-20s: FAIL (Failed to write magic number)\n", "EEPROM");
			return ret;
		}
		
		// Wait for write cycle to complete (typical EEPROM write time)
		udelay(5000);
		
		// Read back to verify
		ret = dm_i2c_read(eeprom_dev, MAGIC_OFFSET, read_back, 4);
		if (ret) {
			printf("%-20s: FAIL (Failed to read back)\n", "EEPROM");
			return ret;
		}
		
		// Verify write
		for (i = 0; i < 4; i++) {
			if (read_back[i] != magic_bytes[i]) {
				printf("%-20s: FAIL (Write verification failed)\n", "EEPROM");
				return -1;
			}
		}
	}
	
	// Drive GPIO3_00 back high to re-enable write protection
	gpio_dat = in_be32((void *)(GPIO3_BASE + GPIO3_GPDAT));
	out_be32((void *)(GPIO3_BASE + GPIO3_GPDAT), gpio_dat | 0x80000000);
	
	printf("%-20s: PASS\n", "EEPROM");
	return 0;
}