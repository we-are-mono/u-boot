/*
 * U-Boot command to program EEPROM with board information
 * Usage: program_eeprom <model> <serial> <mac_start> <mac_end>
 * Example: program_eeprom "Gateway Development Kit" "A2.B002" 02:4D:4F:4E:4F:01 02:4D:4F:4E:4F:05
 */

#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <command.h>
#include <exports.h>
#include <asm/io.h>
#include <linux/ctype.h>

#define EEPROM_ADDR 0x50
#define I2C_BUS 3

#define GPIO3_BASE 0x02320000
#define GPIO3_GPDIR 0x00
#define GPIO3_GPDAT 0x08

/* EEPROM Layout Offsets */
#define OFFSET_MAGIC        0x0000
#define OFFSET_VERSION      0x0004
#define OFFSET_CRC          0x0006
#define OFFSET_MODEL        0x0008
#define OFFSET_SERIAL       0x0028
#define OFFSET_MAC0         0x0068
#define OFFSET_MAC1         0x006E
#define OFFSET_MAC2         0x0074
#define OFFSET_MAC3         0x007A
#define OFFSET_MAC4         0x0080

#define MODEL_SIZE          32
#define SERIAL_SIZE         64
#define MAC_COUNT           5

/* Magic number "MAGC" */
#define MAGIC_BYTE0         0x4D  // 'M'
#define MAGIC_BYTE1         0x41  // 'A'
#define MAGIC_BYTE2         0x47  // 'G'
#define MAGIC_BYTE3         0x43  // 'C'

/* CRC16-CCITT */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;
	size_t i, j;
	
	for (i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (j = 0; j < 8; j++) {
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x1021;
			else
				crc = crc << 1;
		}
	}
	
	return crc;
}

/* Parse MAC address string "XX:XX:XX:XX:XX:XX" to bytes */
static int parse_mac(const char *str, uint8_t *mac)
{
	int i;
	
	for (i = 0; i < 6; i++) {
		char byte_str[3];
		byte_str[0] = str[i * 3];
		byte_str[1] = str[i * 3 + 1];
		byte_str[2] = '\0';
		
		if (!isxdigit(byte_str[0]) || !isxdigit(byte_str[1]))
			return -1;
		
		mac[i] = simple_strtoul(byte_str, NULL, 16);
		
		if (i < 5 && str[i * 3 + 2] != ':')
			return -1;
	}
	
	return 0;
}

/* Increment MAC address by one */
static void increment_mac(uint8_t *mac)
{
	int i;
	
	for (i = 5; i >= 0; i--) {
		if (++mac[i] != 0)
			break;
	}
}

/* Unlock EEPROM by driving GPIO3_00 low */
static void unlock_eeprom(void)
{
	u32 gpio_dir, gpio_dat;
	
	/* Read current values */
	gpio_dir = in_be32((void *)(GPIO3_BASE + GPIO3_GPDIR));
	gpio_dat = in_be32((void *)(GPIO3_BASE + GPIO3_GPDAT));
	
	/* Set GPIO3_00 as output (set bit 31) */
	gpio_dir |= 0x80000000;
	out_be32((void *)(GPIO3_BASE + GPIO3_GPDIR), gpio_dir);
	
	/* Drive GPIO3_00 low (clear bit 31) to unlock EEPROM */
	gpio_dat &= ~0x80000000;
	out_be32((void *)(GPIO3_BASE + GPIO3_GPDAT), gpio_dat);
	
	/* Read back to ensure write completed */
	(void)in_be32((void *)(GPIO3_BASE + GPIO3_GPDAT));
	
	udelay(10000);
}

/* Lock EEPROM by driving GPIO3_00 high */
static void lock_eeprom(void)
{
	u32 gpio_dat;
	
	/* Read current value */
	gpio_dat = in_be32((void *)(GPIO3_BASE + GPIO3_GPDAT));
	
	/* Drive GPIO3_00 high (set bit 31) to lock EEPROM */
	gpio_dat |= 0x80000000;
	out_be32((void *)(GPIO3_BASE + GPIO3_GPDAT), gpio_dat);
}

/* Write string to EEPROM at offset, padded with zeros, respecting page boundaries */
static int write_string(struct udevice *dev, uint16_t offset, const char *str, size_t max_len)
{
	uint8_t buffer[max_len];
	size_t len = strlen(str);
	int ret;
	size_t remaining, chunk_size;
	uint16_t current_offset;
	uint8_t *current_ptr;
	
	if (len >= max_len)
		len = max_len - 1;
	
	memset(buffer, 0, max_len);
	memcpy(buffer, str, len);
	
	/* Write in chunks, respecting 32-byte page boundaries */
	/* Pages are at addresses where bits A11-A5 are the same */
	current_offset = offset;
	current_ptr = buffer;
	remaining = max_len;
	
	while (remaining > 0) {
		/* Calculate bytes remaining in current page */
		uint16_t page_offset = current_offset & 0x1F;  // Bits A4-A0
		chunk_size = 32 - page_offset;  // Bytes left in this page
		
		if (chunk_size > remaining)
			chunk_size = remaining;
		
		ret = dm_i2c_write(dev, current_offset, current_ptr, chunk_size);
		if (ret)
			return ret;
		
		udelay(10000);  // Wait for write cycle to complete
		
		current_offset += chunk_size;
		current_ptr += chunk_size;
		remaining -= chunk_size;
	}
	
	return 0;
}

/* Test if EEPROM is programmed by checking magic number and load to environment */
int test_eeprom(void)
{
	struct udevice *eeprom_dev;
	uint8_t magic_read[4];
	uint8_t model_read[MODEL_SIZE];
	uint8_t serial_read[SERIAL_SIZE];
	uint8_t mac_read[6];
	char mac_str[18];
	int ret, i;
	const char *mac_env_names[] = {"ethaddr", "eth1addr", "eth2addr", "eth3addr", "eth4addr"};
	uint16_t mac_offsets[] = {OFFSET_MAC0, OFFSET_MAC1, OFFSET_MAC2, OFFSET_MAC3, OFFSET_MAC4};
	
	/* Get EEPROM device with 2-byte address offset */
	ret = i2c_get_chip_for_busnum(I2C_BUS, EEPROM_ADDR, 2, &eeprom_dev);
	if (ret) {
		printf("%-20s: FAIL (Device not found at 0x%02x)\n", "EEPROM", EEPROM_ADDR);
		return ret;
	}
	
	/* Read magic number */
	ret = dm_i2c_read(eeprom_dev, OFFSET_MAGIC, magic_read, 4);
	if (ret) {
		printf("%-20s: FAIL (Failed to read)\n", "EEPROM");
		return ret;
	}
	
	/* Check if magic number is valid */
	if (magic_read[0] != MAGIC_BYTE0 ||
	    magic_read[1] != MAGIC_BYTE1 ||
	    magic_read[2] != MAGIC_BYTE2 ||
	    magic_read[3] != MAGIC_BYTE3) {
		printf("%-20s: FAIL (Magic number not found)\n", "EEPROM");
		return -1;
	}
	
	/* Read model */
	ret = dm_i2c_read(eeprom_dev, OFFSET_MODEL, model_read, MODEL_SIZE);
	if (ret) {
		printf("%-20s: FAIL (Failed to read model)\n", "EEPROM");
		return ret;
	}
	model_read[MODEL_SIZE - 1] = '\0';
	
	/* Read serial number */
	ret = dm_i2c_read(eeprom_dev, OFFSET_SERIAL, serial_read, SERIAL_SIZE);
	if (ret) {
		printf("%-20s: FAIL (Failed to read serial)\n", "EEPROM");
		return ret;
	}
	serial_read[SERIAL_SIZE - 1] = '\0';
	
	/* Print PASS with serial number */
	printf("%-20s: PASS (%s)\n", "EEPROM", serial_read);
	
	/* Load model and serial into environment */
	env_set("model", (char *)model_read);
	env_set("serial_number", (char *)serial_read);
	
	/* Read and set MAC addresses */
	for (i = 0; i < MAC_COUNT; i++) {
		ret = dm_i2c_read(eeprom_dev, mac_offsets[i], mac_read, 6);
		if (ret) {
			printf("Warning: Failed to read MAC address %d\n", i);
			continue;
		}
		
		/* Format MAC address as string */
		sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
		        mac_read[0], mac_read[1], mac_read[2],
		        mac_read[3], mac_read[4], mac_read[5]);
		
		/* Set environment variable */
		env_set(mac_env_names[i], mac_str);
	}
	
	return 0;
}

static int do_program_eeprom(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct udevice *eeprom_dev;
	uint8_t data_buffer[256];
	uint8_t mac_start[6], mac_current[6], mac_end[6];
	uint16_t crc;
	int ret, i;
	
	if (argc != 5) {
		printf("Usage: program_eeprom <model> <serial> <mac_start> <mac_end>\n");
		printf("Example: program_eeprom \"Gateway Development Kit\" \"A2.B002\" 02:4D:4F:4E:4F:01 02:4D:4F:4E:4F:05\n");
		return CMD_RET_USAGE;
	}
	
	const char *model = argv[1];
	const char *serial = argv[2];
	const char *mac_start_str = argv[3];
	const char *mac_end_str = argv[4];
	
	/* First, check if EEPROM is already programmed */
	printf("Checking if EEPROM is already programmed...\n");
	ret = test_eeprom();
	if (ret == 0) {
		printf("\nERROR: EEPROM is already programmed!\n");
		printf("Magic number detected. To reprogram, you must manually erase the EEPROM first.\n");
		return CMD_RET_FAILURE;
	}
	printf("EEPROM is blank, proceeding with programming...\n\n");
	
	/* Validate inputs */
	if (strlen(model) >= MODEL_SIZE) {
		printf("Error: Model name too long (max %d characters)\n", MODEL_SIZE - 1);
		return CMD_RET_FAILURE;
	}
	
	if (strlen(serial) >= SERIAL_SIZE) {
		printf("Error: Serial number too long (max %d characters)\n", SERIAL_SIZE - 1);
		return CMD_RET_FAILURE;
	}
	
	/* Parse MAC addresses */
	if (parse_mac(mac_start_str, mac_start) < 0) {
		printf("Error: Invalid MAC start address format\n");
		return CMD_RET_FAILURE;
	}
	
	if (parse_mac(mac_end_str, mac_end) < 0) {
		printf("Error: Invalid MAC end address format\n");
		return CMD_RET_FAILURE;
	}
	
	/* Verify we have exactly 5 MAC addresses in range */
	memcpy(mac_current, mac_start, 6);
	for (i = 0; i < MAC_COUNT - 1; i++) {
		increment_mac(mac_current);
	}
	if (memcmp(mac_current, mac_end, 6) != 0) {
		printf("Error: MAC address range must contain exactly %d addresses\n", MAC_COUNT);
		return CMD_RET_FAILURE;
	}
	
	printf("Programming EEPROM with:\n");
	printf("  Model:  %s\n", model);
	printf("  Serial: %s\n", serial);
	printf("  MAC:    %s to %s\n", mac_start_str, mac_end_str);
	
	/* Unlock EEPROM */
	unlock_eeprom();
	
	/* Get EEPROM device */
	ret = i2c_get_chip_for_busnum(I2C_BUS, EEPROM_ADDR, 2, &eeprom_dev);
	if (ret) {
		printf("Error: Failed to get EEPROM device at bus %d, address 0x%02x\n", 
		       I2C_BUS, EEPROM_ADDR);
		lock_eeprom();
		return CMD_RET_FAILURE;
	}
	
	/* Write Magic Number - all 4 bytes at once */
	printf("Writing magic number...\n");
	data_buffer[0] = 0x4D;  // 'M'
	data_buffer[1] = 0x41;  // 'A'
	data_buffer[2] = 0x47;  // 'G'
	data_buffer[3] = 0x43;  // 'C'
	ret = dm_i2c_write(eeprom_dev, OFFSET_MAGIC, data_buffer, 4);
	if (ret) goto write_error;
	udelay(10000);  // 10ms wait for write cycle
	
	/* Write Format Version - both bytes at once */
	printf("Writing format version...\n");
	data_buffer[0] = 0x00;
	data_buffer[1] = 0x01;
	ret = dm_i2c_write(eeprom_dev, OFFSET_VERSION, data_buffer, 2);
	if (ret) goto write_error;
	udelay(10000);  // 10ms wait for write cycle
	
	/* Write Model */
	printf("Writing model name...\n");
	ret = write_string(eeprom_dev, OFFSET_MODEL, model, MODEL_SIZE);
	if (ret) goto write_error;
	
	/* Write Serial */
	printf("Writing serial number...\n");
	ret = write_string(eeprom_dev, OFFSET_SERIAL, serial, SERIAL_SIZE);
	if (ret) goto write_error;
	
	/* Write MAC addresses */
	printf("Writing MAC addresses...\n");
	memcpy(mac_current, mac_start, 6);
	
	ret = dm_i2c_write(eeprom_dev, OFFSET_MAC0, mac_current, 6);
	if (ret) goto write_error;
	udelay(5000);
	
	increment_mac(mac_current);
	ret = dm_i2c_write(eeprom_dev, OFFSET_MAC1, mac_current, 6);
	if (ret) goto write_error;
	udelay(5000);
	
	increment_mac(mac_current);
	ret = dm_i2c_write(eeprom_dev, OFFSET_MAC2, mac_current, 6);
	if (ret) goto write_error;
	udelay(5000);
	
	increment_mac(mac_current);
	ret = dm_i2c_write(eeprom_dev, OFFSET_MAC3, mac_current, 6);
	if (ret) goto write_error;
	udelay(5000);
	
	increment_mac(mac_current);
	ret = dm_i2c_write(eeprom_dev, OFFSET_MAC4, mac_current, 6);
	if (ret) goto write_error;
	udelay(5000);
	
	/* Read back data section for CRC calculation */
	printf("Calculating CRC...\n");
	ret = dm_i2c_read(eeprom_dev, OFFSET_MODEL, data_buffer, 
	                  OFFSET_MAC4 + 6 - OFFSET_MODEL);
	if (ret) {
		printf("Error: Failed to read back data for CRC\n");
		lock_eeprom();
		return CMD_RET_FAILURE;
	}
	
	/* Calculate and write CRC */
	crc = crc16_ccitt(data_buffer, OFFSET_MAC4 + 6 - OFFSET_MODEL);
	data_buffer[0] = (crc >> 8) & 0xFF;
	data_buffer[1] = crc & 0xFF;
	ret = dm_i2c_write(eeprom_dev, OFFSET_CRC, data_buffer, 2);
	if (ret) goto write_error;
	udelay(10000);
	
	printf("CRC16: 0x%04X\n", crc);
	
	/* Wait for all writes to complete BEFORE locking */
	printf("Waiting for EEPROM write cycles to complete...\n");
	udelay(50000);  // 50ms delay to ensure all writes have settled
	
	/* Verify by reading back BEFORE we lock */
	printf("\nVerifying EEPROM contents:\n");
	ret = dm_i2c_read(eeprom_dev, 0, data_buffer, 160);
	if (ret) {
		printf("Error: Failed to read back for verification\n");
		lock_eeprom();
		return CMD_RET_FAILURE;
	}
	
	printf("Magic:   %02X %02X %02X %02X\n", 
	       data_buffer[0], data_buffer[1], data_buffer[2], data_buffer[3]);
	printf("Version: %02X %02X\n", data_buffer[4], data_buffer[5]);
	printf("CRC:     %02X %02X\n", data_buffer[6], data_buffer[7]);
	printf("Model:   %s\n", (char *)&data_buffer[OFFSET_MODEL]);
	printf("Serial:  %s\n", (char *)&data_buffer[OFFSET_SERIAL]);
	
	for (i = 0; i < MAC_COUNT; i++) {
		uint8_t *mac = &data_buffer[OFFSET_MAC0 - OFFSET_MAGIC + i * 6];
		printf("MAC%d:    %02X:%02X:%02X:%02X:%02X:%02X\n", i,
		       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}
	
	/* NOW lock EEPROM after verification is complete */
	lock_eeprom();
	
	printf("\nEEPROM programming successful!\n");
	return CMD_RET_SUCCESS;
	
write_error:
	printf("Error: Failed to write to EEPROM\n");
	lock_eeprom();
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(
	program_eeprom, 5, 0, do_program_eeprom,
	"Program EEPROM with board information",
	"<model> <serial> <mac_start> <mac_end>\n"
	"    model     - Board model name (max 31 chars)\n"
	"    serial    - Board serial number (max 63 chars)\n"
	"    mac_start - First MAC address (format: XX:XX:XX:XX:XX:XX)\n"
	"    mac_end   - Last MAC address (must be start + 4)\n"
	"Example:\n"
	"    program_eeprom \"Gateway Development Kit\" \"A2.B002\" 02:4D:4F:4E:4F:01 02:4D:4F:4E:4F:05"
);