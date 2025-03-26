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
#include <wdt.h>
#include <watchdog.h>
#include <dm.h>

#include "../common/i2c_mux.h"

DECLARE_GLOBAL_DATA_PTR;

/* 
 * USB ports have this weird reset thing going on
 * so the port doesn't come out of reset with the
 * rest of the CPU, so we have to do it manually
*/
static inline void usb_reset(void)
{
#ifdef CONFIG_HAS_FSL_XHCI_USB
	uint reset_val = 0x9e000000;
	struct ccsr_scfg *scfg = (struct ccsr_scfg *)CFG_SYS_FSL_SCFG_ADDR;
	
	out_le32(&scfg->usb_refclk_selcr1, reset_val);
#endif
}

/* The main RGB LED needs to pulse repeatedly until we get to Linux */
int led_init(void)
{
	u8 led, reg;
	int ret, i, j;
	struct udevice *led_controller, *mux_dev;

	uint8_t val;
	uint8_t animation[] = { 0x00, 0x00, 0x00, 0x80, 0x00, 0x80, 0x00, 0x55, 0x55, 0x3 };
	uint8_t animation_register_ranges[][2] = {
		{ 0x80, 0x89 }, /* LED0, blue */
		{ 0x9A, 0xA3 }, /* LED1, red */
		{ 0xB4, 0xBD }, /* LED2, green */
	};
	
	/* Select i2c mux (IC2-CH2) and set the active channel to 3 */
	i2c_get_chip_for_busnum(2, I2C_MUX_ADDR, 1, &mux_dev);
	uint8_t mux_chan = I2C_RGB_MUX_CHAN;
	dm_i2c_write(mux_dev, 0x00, &mux_chan, 1);

	/* Select LED controller */
	i2c_get_chip_for_busnum(2, I2C_RGB_LED_ADDR, 1, &led_controller);

	/* Enable the LED, set it to direct drive, enable animation */
	reg = 0x01;
	dm_i2c_write(led_controller, 0x0, &reg, 1);
	reg = 0x00;
	dm_i2c_write(led_controller, 0x002, &reg, 1);
	reg = 0x0f;
	dm_i2c_write(led_controller, 0x004, &reg, 1);
	
	reg = 0x55;
	dm_i2c_write(led_controller, 0x010, &reg, 1); /* Confirm changes */

	/* Enable all LEDs */
	reg = 0x0f;
	dm_i2c_write(led_controller, 0x020, &reg, 1);

	/* Set peak current for all LEDs in auto mode */
	uint8_t led_current = I2C_RGB_LED_CURR;
	for (led = 0x50; led <= 0x53; led++) {
		ret = dm_i2c_write(led_controller, led, &led_current, 1);
    }

	/* Animation (white pulsing) */
	for (i = 0; i < ARRAY_SIZE(animation_register_ranges); i++) {
        uint8_t start = animation_register_ranges[i][0];
        uint8_t end = animation_register_ranges[i][1];
		uint8_t range_size = end - start + 1;

        for (j = 0; j < range_size; j++) {
            val = animation[j];
            ret = dm_i2c_write(led_controller, start + j, &val, 1);
        }
    }

	reg = 0x55;
	dm_i2c_write(led_controller, 0x010, &reg, 1); /* Confirm changes */

	reg = 0xff;
	dm_i2c_write(led_controller, 0x011, &reg, 1); /* Start animation */

	return 0;
}

int fan_init(void)
{
	u8 reg;
	uint8_t mux_data = 0x08; // Mux channel 3
	struct udevice *fan_controller, *mux_dev;

	/* Select i2c mux (IC2-CH2) and set the active channel to 3 */
	i2c_get_chip_for_busnum(0, I2C_MUX_ADDR, 1, &mux_dev);
	dm_i2c_write(mux_dev, 0x00, &mux_data, 1);

	/* Select Fan controller */
	i2c_get_chip_for_busnum(0, I2C_FAN_ADDR, 1, &fan_controller);

	/* Invert polarity and set output type to push-pull */
	reg = 0xff;
	dm_i2c_write(fan_controller, 0x2a, &reg, 1);
	dm_i2c_write(fan_controller, 0x2b, &reg, 1);

	/* Set fans to fixed 35% speed, Linux will take care of proper speeds */
	reg = 0x5A;
	dm_i2c_write(fan_controller, 0x30, &reg, 1);
	dm_i2c_write(fan_controller, 0x40, &reg, 1);

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
	fan_init();
	usb_reset();

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

#ifdef CONFIG_WATCHDOG
void board_prep_linux(struct bootm_headers *images)
{
	struct udevice *wdt;
	int ret;

	/* Locate the watchdog device */
	ret = uclass_get_device(UCLASS_WDT, 0, &wdt);

	/* Start watchdog with 15 seconds timeout */
	ret = wdt_start(wdt, 15000, 0);
	if (ret) {
		printf("Failed to start watchdog.\n");		
	}
}
#endif