#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <command.h>
#include <exports.h>
#include "i2c_helpers.h"
#include "NVM_config_STUSB45.h"

#define STUSB4500_ADDR 0x28
#define I2C_BUS 2
#define I2C_MUX_ADDR 0x70
#define I2C_MUX_CHANNEL 0x04
#define FTP_KEY_REG 0x95
#define FTP_CTRL_0_REG 0x96
#define FTP_CTRL_1_REG 0x97
#define FTP_DATA_START 0x53
#define RESET_CTRL_REG 0x23

static int read_nvm_sector(struct udevice *dev, uint8_t sector, uint8_t *data)
{
	int ret;
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, 0x00);
	if (ret)
		return ret;
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x50 | sector);
	if (ret)
		return ret;
	
	udelay(1000);
	
	ret = dm_i2c_read(dev, FTP_DATA_START, data, 8);
	return ret;
}

static int write_nvm_sector(struct udevice *dev, uint8_t sector, const uint8_t *data)
{
	int ret;
	
	for (int i = 0; i < 8; i++) {
		ret = dm_i2c_reg_write(dev, FTP_DATA_START + i, data[i]);
		if (ret)
			return ret;
	}
	
	udelay(1000);
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, 0x01);
	if (ret)
		return ret;
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x50);
	if (ret)
		return ret;
	
	udelay(1000);
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, 0x06);
	if (ret)
		return ret;
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x50 | sector);
	if (ret)
		return ret;
	
	udelay(2000); // 2ms for programming
	
	return 0;
}

static int erase_nvm(struct udevice *dev)
{
	int ret;
	
	printf("Erasing NVM...\n");
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, 0xFA);
	if (ret)
		return ret;
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x50);
	if (ret)
		return ret;
	
	udelay(1000);
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, 0x07);
	if (ret)
		return ret;
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x50);
	if (ret)
		return ret;
	
	udelay(5000);
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_1_REG, 0x05);
	if (ret)
		return ret;
	
	ret = dm_i2c_reg_write(dev, FTP_CTRL_0_REG, 0x50);
	if (ret)
		return ret;
	
	udelay(5000);
	
	return 0;
}

static int program_nvm(struct udevice *dev)
{
	int ret;
	const uint8_t *sector_data;
	
	ret = erase_nvm(dev);
	if (ret) {
		printf("ERROR: Erase failed\n");
		return ret;
	}
	
	for (int sector = 0; sector < 5; sector++) {
		switch(sector) {
			case 0: sector_data = Sector0; break;
			case 1: sector_data = Sector1; break;
			case 2: sector_data = Sector2; break;
			case 3: sector_data = Sector3; break;
			case 4: sector_data = Sector4; break;
			default: return -1;
		}
		
		printf("Writing Sector %d...\n", sector);
		ret = write_nvm_sector(dev, sector, sector_data);
		if (ret) {
			printf("ERROR: Failed to write Sector %d\n", sector);
			return ret;
		}
	}

	return 0;
}

static void stusb4500_reset(struct udevice *dev)
{
	int ret;
	
	ret = dm_i2c_reg_write(dev, RESET_CTRL_REG, 0x01);
	if (ret) {
		printf("WARNING: I2C reset failed, attempting board reset\n");
		do_reset(NULL, 0, 0, NULL);
	}
}

int test_stusb4500_nvm(void)
{
	struct udevice *stusb_dev;
	uint8_t sector_data[8];
	const uint8_t *expected_data;
	int ret;
	int sector;
	int mismatch_found = 0;
	
	ret = setup_i2c_muxed_device(I2C_BUS, I2C_MUX_ADDR, I2C_MUX_CHANNEL,
	                             STUSB4500_ADDR, &stusb_dev);
	if (ret) {
		printf("%-20s: FAIL (Setup failed)\n", "USB PD controller");
		return ret;
	}
	
	// Unlock NVM
	ret = dm_i2c_reg_write(stusb_dev, FTP_KEY_REG, 0x47);
	if (ret) {
		printf("ERROR: Failed to unlock NVM: %d\n", ret);
		return ret;
	}
	
	// Reset and power up NVM controller
	ret = dm_i2c_reg_write(stusb_dev, FTP_DATA_START, 0x00);
	if (ret)
		return ret;
	
	ret = dm_i2c_reg_write(stusb_dev, FTP_CTRL_0_REG, 0x00);
	if (ret)
		return ret;
	
	udelay(1000);
	
	ret = dm_i2c_reg_write(stusb_dev, FTP_CTRL_0_REG, 0x40);
	if (ret) {
		printf("ERROR: Failed to power up NVM: %d\n", ret);
		return ret;
	}
	
	for (sector = 0; sector < 5; sector++) {
		switch(sector) {
			case 0: expected_data = Sector0; break;
			case 1: expected_data = Sector1; break;
			case 2: expected_data = Sector2; break;
			case 3: expected_data = Sector3; break;
			case 4: expected_data = Sector4; break;
			default: expected_data = NULL;
		}
		
		ret = read_nvm_sector(stusb_dev, sector, sector_data);
		if (ret) {
			printf("Sector %d: ERROR - Read failed (%d)\n", sector, ret);
			mismatch_found = 1;
			continue;
		}
		
		// Compare
		for (int i = 0; i < 8; i++) {
			if (sector_data[i] != expected_data[i]) {
				mismatch_found = 1;
				break;
			}
		}
	}
	
	if (mismatch_found) {
		printf("\n*** USB PD CONFIGURATION MISMATCH DETECTED - REPROGRAMMING NVM ***\n\n");
		
		ret = program_nvm(stusb_dev);
		if (ret) {
			printf("ERROR: NVM programming failed: %d\n", ret);
			dm_i2c_reg_write(stusb_dev, FTP_KEY_REG, 0x00);
			return ret;
		}
		
		// Exit test mode
		uint8_t exit_data[2] = {0x40, 0x00};
		dm_i2c_write(stusb_dev, FTP_CTRL_0_REG, exit_data, 2);
		dm_i2c_reg_write(stusb_dev, FTP_KEY_REG, 0x00);
		
		printf("\n*** NVM REPROGRAMMED - RESETTING STUSB4500 ***\n");
		
		// Reset STUSB4500 via I2C (this will reset the board)
		stusb4500_reset(stusb_dev);
		
		// Should not reach this
		return 0;
	}
	
	// Lock NVM
	dm_i2c_reg_write(stusb_dev, FTP_KEY_REG, 0x00);

	printf("%-20s: PASS\n", "USB PD controller");
	return 0;
}