/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2025 Mono Technologies Inc.
 */

#ifndef __GATEWAY_DK_H__
#define __GATEWAY_DK_H__

#include <asm/arch/config.h>
#include <asm/arch/stream_id_lsch2.h>

#define CFG_SYS_DDR_SDRAM_BASE		0x80000000
#define CFG_SYS_FSL_DDR_SDRAM_BASE_PHY	0
#define CFG_SYS_SDRAM_BASE			CFG_SYS_DDR_SDRAM_BASE
#define CFG_SYS_DDR_BLOCK2_BASE		0x880000000ULL
#define CPU_RELEASE_ADDR			secondary_boot_addr
#define CFG_SYS_UBOOT_BASE			0x40100000

#define I2C0_BUS_ADDR				0x0
#define I2C_MUX_ADDR				0x70

/* Serial Port */
#define CFG_SYS_NS16550_CLK			(get_serial_clock())

/* I2C bus multiplexer */
#define I2C_MUX_PCA_ADDR_PRI		0x70 /* Primary Mux*/
#define I2C_MUX_CH_DEFAULT			0x1 /* Channel 0*/
#define I2C_MUX_CH_RTC				0x1 /* Channel 0*/

/* RTC */
#define CFG_SYS_I2C_RTC_ADDR		0x51  /* Channel 0 I2C bus 0*/
#define CFG_SYS_RTC_BUS_NUM			0

/* FMan ucode */
#ifndef SPL_NO_FMAN
#ifdef CONFIG_SYS_DPAA_FMAN
#define CFG_SYS_FM_MURAM_SIZE		0x60000
#endif
#endif

/* Misc */
#define HWCONFIG_BUFFER_SIZE		128

/*
 * Environment
 */
#define CFG_SYS_FSL_QSPI_BASE		0x40000000

#undef BOOT_TARGET_DEVICES
#define BOOT_TARGET_DEVICES(func) \
	func(MMC, mmc, 0) \
	func(USB, usb, 0) \
	func(DHCP, dhcp, na)

#include <config_distro_bootcmd.h>

/* FMan */
#ifdef CONFIG_SYS_DPAA_FMAN
#define SGMII_PHY1_ADDR				0x0
#define SGMII_PHY2_ADDR				0x1
#define SGMII_PHY3_ADDR				0x2
#define FDT_SEQ_MACADDR_FROM_ENV
#endif

#include <asm/fsl_secure_boot.h>
#include <asm/arch/soc.h>

#endif /* __GATEWAY_DK_H__ */
