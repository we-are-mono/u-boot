// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025 Mono Technologies Inc.
 */

#include <common.h>
#include <i2c.h>
#include <fdt_support.h>
#include <env.h>
#include <init.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <asm/arch/fsl_serdes.h>
#include <asm/arch/soc.h>
#include <asm/arch-fsl-layerscape/fsl_icid.h>
#include <asm/gpio.h>
#include <hwconfig.h>
#include <ahci.h>
#include <mmc.h>
#include <scsi.h>
#include <fm_eth.h>
#include <fsl_csu.h>
#include <fsl_esdhc.h>
#include <fsl_dspi.h>

#include "../common/i2c_mux.h"

DECLARE_GLOBAL_DATA_PTR;

#define I2C_MUX_ADDR  0x70
#define I2C_RGB_LED_ADDR  0x6c

int led_init(void)
{
	u8 reg, off = 0x00;
	uint8_t mux_data = 0x08; // Mux channel 3
	struct udevice *led_controller, *mux_dev;

	/* Select i2c mux (IC2-CH2) and set the active channel to 3 */
	i2c_get_chip_for_busnum(2, I2C_MUX_ADDR, 1, &mux_dev);
	dm_i2c_write(mux_dev, 0x00, &mux_data, 1);

	/* Select LED controller */
	i2c_get_chip_for_busnum(2, I2C_RGB_LED_ADDR, 1, &led_controller);

	/* Enable the LED and set it to direct drive */
	reg = 0x01;
	dm_i2c_write(led_controller, 0x0, &reg, 1);
	reg = 0x00;
	dm_i2c_write(led_controller, 0x002, &reg, 1);
	reg = 0x55;
	dm_i2c_write(led_controller, 0x010, &reg, 1); /* Confirm changes */

	/* Enable all LEDs */
	reg = 0x0f;
	dm_i2c_write(led_controller, 0x020, &reg, 1);

	/* Set peak current for all LEDs */
	reg = 0x7f;
	dm_i2c_write(led_controller, 0x30, &reg, 1); // blue
	dm_i2c_write(led_controller, 0x31, &reg, 1); // red
	dm_i2c_write(led_controller, 0x32, &reg, 1); // green

	/* Turn green LED on, 20% brightness */
	reg = 0x33;
	dm_i2c_write(led_controller, 0x40, &off, 1); // blue
	dm_i2c_write(led_controller, 0x41, &off, 1); // red
	dm_i2c_write(led_controller, 0x42, &reg, 1); // green

	return 0;

}

int board_early_init_f(void)
{
	fsl_lsch2_early_init_f();

	return 0;
}

int checkboard(void)
{
	return 0;
}

int board_setup_core_volt(u32 vdd)
{
	return 0;
}

#ifdef CONFIG_MISC_INIT_R
int misc_init_r(void)
{
	return 0;
}
#endif

int fsl_board_late_init(void)
{
	led_init();
	return 0;
}

int board_init(void)
{
#ifdef CONFIG_NXP_ESBC
	/*
	 * In case of Secure Boot, the IBR configures the SMMU
	 * to allow only Secure transactions.
	 * SMMU must be reset in bypass mode.
	 * Set the ClientPD bit and Clear the USFCFG Bit
	 */
	u32 val;
	val = (in_le32(SMMU_SCR0) | SCR0_CLIENTPD_MASK) & ~(SCR0_USFCFG_MASK);
	out_le32(SMMU_SCR0, val);
	val = (in_le32(SMMU_NSCR0) | SCR0_CLIENTPD_MASK) & ~(SCR0_USFCFG_MASK);
	out_le32(SMMU_NSCR0, val);
#endif

	return 0;
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	u64 base[CONFIG_NR_DRAM_BANKS];
	u64 size[CONFIG_NR_DRAM_BANKS];

	/* fixup DT for the two DDR banks */
	base[0] = gd->bd->bi_dram[0].start;
	size[0] = gd->bd->bi_dram[0].size;

	base[1] = gd->bd->bi_dram[1].start;
	size[1] = gd->bd->bi_dram[1].size;

	fdt_fixup_memory_banks(blob, base, size, 2);
	ft_cpu_setup(blob, bd);

#ifdef CONFIG_SYS_DPAA_FMAN
#ifndef CONFIG_DM_ETH
	fdt_fixup_fman_ethernet(blob);
#endif
#endif

	fdt_fixup_icid(blob);
	return 0;
}