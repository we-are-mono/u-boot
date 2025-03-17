// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025 Mono Technologies Inc.
 */
#include <common.h>
#include <fdt_support.h>
#include <net.h>
#include <asm/io.h>
#include <netdev.h>
#include <fm_eth.h>
#include <fsl_dtsec.h>
#include <fsl_mdio.h>
#include <malloc.h>

#include "../common/fman.h"

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

