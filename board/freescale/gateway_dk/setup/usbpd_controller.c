/*
 * STUSB4500 USB PD Controller NVM Programming
 *
 * Improved implementation with:
 * - Status register polling instead of fixed delays
 * - Read-back verification after programming
 * - Bulk I2C writes for sector data
 * - Skip programming if NVM already matches expected values
 *
 * Based on original U-Boot implementation, improved with techniques from
 * Jeff Longo's stusb4500 library.
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <stdio.h>
#include <string.h>
#include <dm.h>
#include <i2c.h>
#include <command.h>
#include <exports.h>
#include "i2c_helpers.h"
#include "NVM_config_STUSB45.h"

#define STUSB4500_ADDR    0x28
#define I2C_BUS           2
#define I2C_MUX_ADDR      0x70
#define I2C_MUX_CHANNEL   0x04

/* Register addresses */
#define FTP_CUST_PASSWORD_REG  0x95
#define FTP_CTRL_0_REG         0x96
#define FTP_CTRL_1_REG         0x97
#define RW_BUFFER              0x53
#define RESET_CTRL_REG         0x23

/* FTP_CTRL_0 bit definitions */
#define FTP_CUST_PWR      0x80  /* Power */
#define FTP_CUST_RST_N    0x40  /* Reset (active low) */
#define FTP_CUST_REQ      0x10  /* Request operation */
#define FTP_CUST_SECT     0x07  /* Sector selection mask */

/* FTP_CTRL_1 bit definitions */
#define FTP_CUST_SER      0xF8  /* Sectors to erase mask */
#define FTP_CUST_OPCODE   0x07  /* Opcode mask */

/* Opcodes */
#define OP_READ           0x00  /* Read sector */
#define OP_WRITE_PL       0x01  /* Write to Program Load register */
#define OP_WRITE_SER      0x02  /* Write to Sector Erase register */
#define OP_READ_PL        0x03  /* Read Program Load register */
#define OP_READ_SER       0x04  /* Read Sector Erase register */
#define OP_ERASE_SECTOR   0x05  /* Erase sectors masked by SER */
#define OP_PROG_SECTOR    0x06  /* Program sector */
#define OP_SOFT_PROG      0x07  /* Soft program sectors masked by SER */

/* Sector masks for SER register */
#define SECTOR0           0x01
#define SECTOR1           0x02
#define SECTOR2           0x04
#define SECTOR3           0x08
#define SECTOR4           0x10
#define ALL_SECTORS       (SECTOR0 | SECTOR1 | SECTOR2 | SECTOR3 | SECTOR4)

/* Constants */
#define FTP_CUST_PASSWORD 0x47
#define NUM_SECTORS       5
#define SECTOR_SIZE       8
#define NVM_SIZE          (NUM_SECTORS * SECTOR_SIZE)
#define POLL_TIMEOUT_US   50000  /* 50ms timeout for operations */
#define POLL_INTERVAL_US  100

/**
 * wait_for_completion - Poll FTP_CTRL_0 until REQ bit clears
 * @dev: I2C device
 *
 * Returns 0 on success, negative error code on failure or timeout
 */
static int wait_for_completion(struct udevice *dev)
{
	uint8_t status;
	int timeout = POLL_TIMEOUT_US;
	int ret;

	do {
		ret = dm_i2c_read(dev, FTP_CTRL_0_REG, &status, 1);
		if (ret)
			return ret;

		if (!(status & FTP_CUST_REQ))
			return 0;

		udelay(POLL_INTERVAL_US);
		timeout -= POLL_INTERVAL_US;
	} while (timeout > 0);

	printf("ERROR: NVM operation timed out\n");
	return -ETIMEDOUT;
}

/**
 * get_sector_data - Get pointer to expected sector data from header
 * @sector: Sector number (0-4)
 *
 * Returns pointer to sector data array, or NULL if invalid sector
 */
static const uint8_t *get_sector_data(int sector)
{
	switch (sector) {
	case 0: return Sector0;
	case 1: return Sector1;
	case 2: return Sector2;
	case 3: return Sector3;
	case 4: return Sector4;
	default: return NULL;
	}
}

/**
 * read_nvm_sector - Read a single sector from NVM
 * @dev: I2C device
 * @sector: Sector number (0-4)
 * @data: Buffer to store 8 bytes of sector data
 *
 * Returns 0 on success, negative error code on failure
 */
static int read_nvm_sector(struct udevice *dev, uint8_t sector, uint8_t *data)
{
	uint8_t buffer;
	int ret;

	/* Set PWR and RST_N bits */
	buffer = FTP_CUST_PWR | FTP_CUST_RST_N;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	/* Write read opcode */
	buffer = OP_READ & FTP_CUST_OPCODE;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, buffer);
	if (ret)
		return ret;

	/* Select sector and issue read command */
	buffer = (sector & FTP_CUST_SECT) | FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	/* Wait for read to complete */
	ret = wait_for_completion(dev);
	if (ret)
		return ret;

	/* Read sector data from buffer */
	ret = dm_i2c_read(dev, RW_BUFFER, data, SECTOR_SIZE);
	if (ret)
		return ret;

	/* Reset controller state */
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x00);

	return ret;
}

/**
 * write_nvm_sector - Write a single sector to NVM
 * @dev: I2C device
 * @sector: Sector number (0-4)
 * @data: 8 bytes of data to write
 *
 * Returns 0 on success, negative error code on failure
 */
static int write_nvm_sector(struct udevice *dev, uint8_t sector, const uint8_t *data)
{
	uint8_t buffer;
	int ret;

	/* Write 8 bytes to RW_BUFFER in a single transaction */
	ret = dm_i2c_write(dev, RW_BUFFER, data, SECTOR_SIZE);
	if (ret)
		return ret;

	/* Set PWR and RST_N bits */
	buffer = FTP_CUST_PWR | FTP_CUST_RST_N;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	/* Write PL (Program Load) opcode */
	buffer = OP_WRITE_PL & FTP_CUST_OPCODE;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, buffer);
	if (ret)
		return ret;

	/* Issue PL write command */
	buffer = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	/* Wait for PL write to complete */
	ret = wait_for_completion(dev);
	if (ret)
		return ret;

	/* Write program sector opcode */
	buffer = OP_PROG_SECTOR & FTP_CUST_OPCODE;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, buffer);
	if (ret)
		return ret;

	/* Issue program command with sector selection */
	buffer = (sector & FTP_CUST_SECT) | FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	/* Wait for programming to complete */
	ret = wait_for_completion(dev);

	return ret;
}

/**
 * erase_nvm - Erase all NVM sectors
 * @dev: I2C device
 *
 * Returns 0 on success, negative error code on failure
 */
static int erase_nvm(struct udevice *dev)
{
	uint8_t buffer;
	int ret;

	printf("Erasing NVM...\n");

	/* Write sectors to erase (all) and SER write opcode */
	buffer = ((ALL_SECTORS << 3) & FTP_CUST_SER) | (OP_WRITE_SER & FTP_CUST_OPCODE);
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, buffer);
	if (ret)
		return ret;

	/* Issue SER write command */
	buffer = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	ret = wait_for_completion(dev);
	if (ret)
		return ret;

	/* Write soft program opcode */
	buffer = OP_SOFT_PROG & FTP_CUST_OPCODE;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, buffer);
	if (ret)
		return ret;

	/* Issue soft program command */
	buffer = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	ret = wait_for_completion(dev);
	if (ret)
		return ret;

	/* Write erase opcode */
	buffer = OP_ERASE_SECTOR & FTP_CUST_OPCODE;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, buffer);
	if (ret)
		return ret;

	/* Issue erase command */
	buffer = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	ret = wait_for_completion(dev);

	return ret;
}

/**
 * enter_read_mode - Unlock NVM and power up for reading
 * @dev: I2C device
 *
 * Returns 0 on success, negative error code on failure
 */
static int enter_read_mode(struct udevice *dev)
{
	uint8_t buffer;
	int ret;

	/* Unlock NVM with password */
	ret = dm_i2c_reg_write(dev, FTP_CUST_PASSWORD_REG, FTP_CUST_PASSWORD);
	if (ret)
		return ret;

	/* Reset internal controller */
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x00);
	if (ret)
		return ret;

	/* Power up NVM controller */
	buffer = FTP_CUST_PWR | FTP_CUST_RST_N;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);

	return ret;
}

/**
 * enter_write_mode - Unlock NVM, power up, and erase for writing
 * @dev: I2C device
 *
 * Returns 0 on success, negative error code on failure
 */
static int enter_write_mode(struct udevice *dev)
{
	uint8_t buffer;
	int ret;

	/* Unlock NVM with password */
	ret = dm_i2c_reg_write(dev, FTP_CUST_PASSWORD_REG, FTP_CUST_PASSWORD);
	if (ret)
		return ret;

	/* RW_BUFFER must be 0 for partial erase feature */
	ret = dm_i2c_reg_write(dev, RW_BUFFER, 0x00);
	if (ret)
		return ret;

	/* Reset internal controller */
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x00);
	if (ret)
		return ret;

	/* Power up NVM controller */
	buffer = FTP_CUST_PWR | FTP_CUST_RST_N;
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, buffer);
	if (ret)
		return ret;

	/* Erase all sectors */
	ret = erase_nvm(dev);

	return ret;
}

/**
 * exit_rw_mode - Power down NVM and lock with password clear
 * @dev: I2C device
 *
 * Returns 0 on success, negative error code on failure
 */
static int exit_rw_mode(struct udevice *dev)
{
	int ret;

	/* Clear FTP_CTRL_0, keep RST_N set */
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, FTP_CUST_RST_N);
	if (ret)
		return ret;

	/* Clear FTP_CTRL_1 */
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, 0x00);
	if (ret)
		return ret;

	/* Lock NVM by clearing password */
	ret = dm_i2c_reg_write(dev, FTP_CUST_PASSWORD_REG, 0x00);

	return ret;
}

/**
 * read_all_sectors - Read entire NVM contents
 * @dev: I2C device
 * @nvm: Buffer to store all sector data [NUM_SECTORS][SECTOR_SIZE]
 *
 * Returns 0 on success, negative error code on failure
 */
static int read_all_sectors(struct udevice *dev, uint8_t nvm[NUM_SECTORS][SECTOR_SIZE])
{
	int ret;
	int sector;

	for (sector = 0; sector < NUM_SECTORS; sector++) {
		ret = read_nvm_sector(dev, sector, nvm[sector]);
		if (ret) {
			printf("ERROR: Failed to read sector %d: %d\n", sector, ret);
			return ret;
		}
	}

	return 0;
}

/**
 * verify_nvm_contents - Compare NVM contents against expected values
 * @nvm: NVM data to verify [NUM_SECTORS][SECTOR_SIZE]
 *
 * Returns 0 if match, 1 if mismatch, negative on error
 */
static int verify_nvm_contents(uint8_t nvm[NUM_SECTORS][SECTOR_SIZE])
{
	const uint8_t *expected;
	int sector;

	for (sector = 0; sector < NUM_SECTORS; sector++) {
		expected = get_sector_data(sector);
		if (!expected)
			return -EINVAL;

		if (memcmp(nvm[sector], expected, SECTOR_SIZE) != 0)
			return 1;  /* Mismatch */
	}

	return 0;  /* Match */
}

/**
 * program_all_sectors - Write all sectors to NVM
 * @dev: I2C device
 *
 * Returns 0 on success, negative error code on failure
 */
static int program_all_sectors(struct udevice *dev)
{
	const uint8_t *sector_data;
	int ret;
	int sector;

	for (sector = 0; sector < NUM_SECTORS; sector++) {
		sector_data = get_sector_data(sector);
		if (!sector_data)
			return -EINVAL;

		printf("Writing sector %d...\n", sector);
		ret = write_nvm_sector(dev, sector, sector_data);
		if (ret) {
			printf("ERROR: Failed to write sector %d: %d\n", sector, ret);
			return ret;
		}
	}

	return 0;
}

/**
 * test_stusb4500_nvm - Main entry point for NVM test/programming
 *
 * Reads NVM, compares against expected values, reprograms if needed,
 * and verifies the result.
 *
 * Returns 0 on success, negative error code on failure
 */
int test_stusb4500_nvm(void)
{
	struct udevice *stusb_dev;
	uint8_t nvm[NUM_SECTORS][SECTOR_SIZE];
	int ret;
	int verify_result;

	/* Set up I2C access through mux */
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             STUSB4500_ADDR, &stusb_dev);
	if (ret) {
		printf("%-20s: FAIL (I2C setup failed)\n", "USB PD controller");
		return ret;
	}

	/* Enter read mode and read current NVM contents */
	ret = enter_read_mode(stusb_dev);
	if (ret) {
		printf("ERROR: Failed to enter read mode: %d\n", ret);
		return ret;
	}

	ret = read_all_sectors(stusb_dev, nvm);
	if (ret) {
		exit_rw_mode(stusb_dev);
		return ret;
	}

	/* Check if NVM already matches expected values */
	verify_result = verify_nvm_contents(nvm);
	if (verify_result == 0) {
		/* NVM already correct - no programming needed */
		exit_rw_mode(stusb_dev);
		printf("%-20s: PASS\n", "USB PD controller");
		return 0;
	}

	/* Mismatch detected - need to reprogram */
	printf("\nUSB PD configuration mismatch detected - reprogramming NVM...\n\n");

	/* Exit read mode before entering write mode */
	ret = exit_rw_mode(stusb_dev);
	if (ret) {
		printf("ERROR: Failed to exit read mode: %d\n", ret);
		return ret;
	}

	/* Enter write mode (unlocks, powers up, and erases) */
	ret = enter_write_mode(stusb_dev);
	if (ret) {
		printf("ERROR: Failed to enter write mode: %d\n", ret);
		exit_rw_mode(stusb_dev);
		return ret;
	}

	/* Program all sectors */
	ret = program_all_sectors(stusb_dev);
	if (ret) {
		printf("ERROR: NVM programming failed: %d\n", ret);
		exit_rw_mode(stusb_dev);
		return ret;
	}

	/* Exit write mode */
	ret = exit_rw_mode(stusb_dev);
	if (ret) {
		printf("ERROR: Failed to exit write mode: %d\n", ret);
		return ret;
	}

	/* Verify by reading back */
	printf("Verifying NVM contents...\n");

	ret = enter_read_mode(stusb_dev);
	if (ret) {
		printf("ERROR: Failed to re-enter read mode for verification: %d\n", ret);
		return ret;
	}

	ret = read_all_sectors(stusb_dev, nvm);
	if (ret) {
		exit_rw_mode(stusb_dev);
		return ret;
	}

	exit_rw_mode(stusb_dev);

	/* Final verification */
	verify_result = verify_nvm_contents(nvm);
	if (verify_result != 0) {
		printf("%-20s: FAIL (verification failed after programming)\n", "USB PD controller");
		return -EIO;
	}

	printf("\n*** NVM reprogrammed and verified successfully! ***\n");
	printf("*** Reset the device for updated settings to take effect. ***\n\n");
	printf("%-20s: PASS (reprogrammed)\n", "USB PD controller");

	return 0;
}