// SPDX-License-Identifier: GPL-2.0+
/*
 * SFP I2C Mux Reset/Self-Test
 *
 * Copyright 2025 Mono Technologies Inc.
 */

#include <linux/types.h>
#include <stdio.h>
#include <dm.h>
#include <i2c.h>
#include "i2c_helpers.h"

#define SFP_MUX_ADDR        0x70
#define I2C_BUS             1      /* i2c1 - SFP bus */
#define PCA9545_CTRL_REG    0x00

/*
 * Reset PCA9545 I2C mux on SFP bus
 *
 * The mux can get stuck with multiple channels enabled after power glitches
 * or abnormal resets, causing I2C arbitration errors. Writing 0x00 disables
 * all channels, resetting the mux to a known good state.
 */
int test_sfp_i2c_mux(void)
{
	struct udevice *mux_dev;
	uint8_t ctrl_val;
	int ret;

	/* Get mux device directly (no channel selection needed) */
	ret = i2c_get_chip_for_busnum(I2C_BUS, SFP_MUX_ADDR, 1, &mux_dev);
	if (ret) {
		printf("%-20s: FAIL (Mux not found on bus %d)\n", "SFP I2C mux", I2C_BUS);
		return ret;
	}

	/* Reset mux - disable all channels */
	ctrl_val = 0x00;
	ret = dm_i2c_write(mux_dev, PCA9545_CTRL_REG, &ctrl_val, 1);
	if (ret) {
		printf("%-20s: FAIL (Reset failed)\n", "SFP I2C mux");
		return ret;
	}

	/* Verify reset by reading back */
	ret = dm_i2c_reg_read(mux_dev, PCA9545_CTRL_REG);
	if (ret < 0) {
		printf("%-20s: FAIL (Read-back failed)\n", "SFP I2C mux");
		return ret;
	}

	if ((ret & 0x0F) != 0x00) {
		printf("%-20s: FAIL (Reset verify: expected 0x00, got 0x%02x)\n",
		       "SFP I2C mux", ret & 0x0F);
		return -1;
	}

	printf("%-20s: PASS (Reset OK)\n", "SFP I2C mux");
	return 0;
}
