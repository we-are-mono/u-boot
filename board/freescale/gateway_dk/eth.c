// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025 Mono Technologies Inc.
 */
#include <common.h>
#include <command.h>
#include <netdev.h>
#include <malloc.h>
#include <fsl_mdio.h>
#include <miiphy.h>
#include <phy.h>
#include <fm_eth.h>
#include <asm/io.h>
#include <exports.h>
#include <asm/arch/fsl_serdes.h>
#include <fsl-mc/fsl_mc.h>

/*
 * Because there are no official u-boot drivers for the GPY115
 * we have to do some basic stuff here since the generic driver
 * doesn't do a good enough job
*/
int board_phy_config(struct phy_device *phydev)
{
	/* To make sure we don't touch any PHYs that might be in SFP+ */
	if (phydev->phy_id == 0x67c9df10) {
		/* First, reset the PHY and give it 10ms to boot up */
		phy_write(phydev, MDIO_DEVAD_NONE, 0x0, 0x8000);
		udelay(10000);

		/* Invert LED polarity, we're driving them from PHYs, not VCC */
		phy_write(phydev, MDIO_DEVAD_NONE, 0x1b, 0xf00);

		/* LED 1 (green) should blink on TX/RX */
		phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x01, 0x0fe0);

		/* LED 2 (amber) should be on when link is up */
		phy_write_mmd(phydev, MDIO_MMD_VEND1, 0x02, 0x2040);
	}

	if (phydev->drv->config)
		phydev->drv->config(phydev);

	return 0;
}

/* Attach PHYs to the MDIO bus */
int board_eth_init(struct bd_info *bis)
{
#ifdef CONFIG_FMAN_ENET
	struct memac_mdio_info dtsec_mdio_info;
	struct mii_dev *dev;
	u32 srds_s1;
	struct ccsr_gur *gur = (void *)(CFG_SYS_FSL_GUTS_ADDR);

	srds_s1 = in_be32(&gur->rcwsr[4]) &
			FSL_CHASSIS2_RCWSR4_SRDS1_PRTCL_MASK;
	srds_s1 >>= FSL_CHASSIS2_RCWSR4_SRDS1_PRTCL_SHIFT;

	/* TODO: Add a SerDes 2 check */
	if (srds_s1 != 0x1133) {
		printf("Invalid SerDes protocol 0x%x for Gateway Development Kit\n",
		       srds_s1);
	}

	dtsec_mdio_info.regs =
		(struct memac_mdio_controller *)CFG_SYS_FM1_DTSEC_MDIO_ADDR;

	dtsec_mdio_info.name = DEFAULT_FM_MDIO_NAME;

	/* Register the 1G MDIO bus */
	fm_memac_mdio_init(bis, &dtsec_mdio_info);

	/* SGMII on MAC 5, 6 (SerDes 1) and 2 (SerDes 2) */
	fm_info_set_phy_address(FM1_DTSEC5, SGMII_PHY1_ADDR);
	fm_info_set_phy_address(FM1_DTSEC6, SGMII_PHY2_ADDR);
	fm_info_set_phy_address(FM1_DTSEC2, SGMII_PHY3_ADDR);

	dev = miiphy_get_dev_by_name(DEFAULT_FM_MDIO_NAME);
	fm_info_set_mdio(FM1_DTSEC5, dev);
	fm_info_set_mdio(FM1_DTSEC6, dev);
	fm_info_set_mdio(FM1_DTSEC2, dev);

	cpu_eth_init(bis);
#endif

	return pci_eth_init(bis);
}

#ifdef CONFIG_FMAN_ENET
int fdt_update_ethernet_dt(void *blob)
{
	return 0;
}
#endif

