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

int test_voltage_sensors(void);
int test_stusb4500_nvm(void);
int test_hd3ss3220(void);
int test_pcf2131_rtc(void);
int test_eeprom(void);
int test_ds100df410_retimer(void);
int test_6v49205b_clkgen(void);
int test_emc2302_fan(void);
int test_tmp431_temperatures(void);
int test_lp5810a_led(int any_test_failed);

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

// As described in the errata document. 
// Some addresses have no description to what they are. 
#define DCFG_CCSR_PORSR1   0x01EE0000
#define DCFG_WRITE_BACK    0x20140000
#define SOME_ADDR          (0x01570000 + 0x1A8)

void workaround_a008127(void)
{
    u32 dat;

    dat = in_le32((void *)DCFG_CCSR_PORSR1);
    dat &= ~RCW_SRC_MASK;             /* Clear RCW_SRC bits */
    out_le32((void *)DCFG_WRITE_BACK, dat);

    out_le32((void *)SOME_ADDR, 0xFFFFFFFF);
}

int board_early_init_f(void)
{
	// Errata fix 008127
	// Needed for SFP functionality when booting from EMMC.
	workaround_a008127();

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
	int test_failed = 0;
	
	printf("\n=== On-board devices self test ===\n\n");

	test_failed |= test_stusb4500_nvm();
	test_failed |= test_voltage_sensors();
	test_failed |= test_tmp431_temperatures();
	test_failed |= test_hd3ss3220();
	test_failed |= test_pcf2131_rtc();
	test_failed |= test_eeprom();
	test_failed |= test_ds100df410_retimer();
	test_failed |= test_6v49205b_clkgen();
	test_failed |= test_emc2302_fan();
	
	// LED test runs last and turns red in case any of the tests fail
	test_lp5810a_led(test_failed);
	
	printf("\n\n");

	return 0;
}
#endif

int fsl_board_late_init(void)
{
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