// SPDX-License-Identifier: GPL-2.0
/*
 * dwmac-imx.c - DWMAC Specific Glue layer for NXP imx8
 *
 * Copyright 2020 NXP
 *
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/stmmac.h>
#include <linux/regulator/consumer.h>

#include "stmmac_platform.h"

#ifdef CONFIG_IMX_SCU_SOC
#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/firmware/imx/sci.h>
#endif

#define GPR_ENET_QOS_INTF_MODE_MASK	GENMASK(21, 16)
#define GPR_ENET_QOS_INTF_SEL_MII	(0x0 << 16)
#define GPR_ENET_QOS_INTF_SEL_RMII	(0x4 << 16)
#define GPR_ENET_QOS_INTF_SEL_RGMII	(0x1 << 16)
#define GPR_ENET_QOS_CLK_GEN_EN		(0x1 << 19)
#define GPR_ENET_QOS_CLK_TX_CLK_SEL	(0x1 << 20)
#define GPR_ENET_QOS_RGMII_EN		(0x1 << 21)

struct imx_dwmac_ops {
	u32 addr_width;
	bool mac_txclk_auto_adj;

	int (*set_intf_mode)(struct plat_stmmacenet_data *plat_dat);
	int (*set_stop_mode)(struct plat_stmmacenet_data *plat_dat, bool is_en);
};

struct imx_priv_data {
	struct device *dev;
	struct clk *clk_tx;
	struct clk *clk_mem;
	struct regmap *intf_regmap;
	u32 intf_reg_off;
	bool rmii_refclk_ext;

	const struct imx_dwmac_ops *ops;
	struct plat_stmmacenet_data *plat_dat;
	struct regulator *phy_supply;
};

static int imx8mp_set_intf_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct imx_priv_data *dwmac = plat_dat->bsp_priv;
	int val;

	switch (plat_dat->interface) {
	case PHY_INTERFACE_MODE_MII:
		val = GPR_ENET_QOS_INTF_SEL_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val = GPR_ENET_QOS_INTF_SEL_RMII;
		val |= (dwmac->rmii_refclk_ext ? 0 : GPR_ENET_QOS_CLK_TX_CLK_SEL);
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = GPR_ENET_QOS_INTF_SEL_RGMII |
		      GPR_ENET_QOS_RGMII_EN;
		break;
	default:
		pr_err("imx dwmac doesn't support %d interface\n",
		       plat_dat->interface);
		return -EINVAL;
	}

	val |= GPR_ENET_QOS_CLK_GEN_EN;
	return regmap_update_bits(dwmac->intf_regmap, dwmac->intf_reg_off,
				  GPR_ENET_QOS_INTF_MODE_MASK, val);
};

static int
imx8dxl_set_intf_mode(struct plat_stmmacenet_data *plat_dat)
{
	int ret = 0;
#ifdef CONFIG_IMX_SCU_SOC
	struct imx_sc_ipc *ipc_handle;
	int val;

	ret = imx_scu_get_handle(&ipc_handle);
	if (ret)
		return ret;

	switch (plat_dat->interface) {
	case PHY_INTERFACE_MODE_MII:
		val = GPR_ENET_QOS_INTF_SEL_MII;
		break;
	case PHY_INTERFACE_MODE_RMII:
		val = GPR_ENET_QOS_INTF_SEL_RMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		val = GPR_ENET_QOS_INTF_SEL_RGMII;
		break;
	default:
		pr_err("imx dwmac doesn't support %d interface\n",
		       plat_dat->interface);
		return -EINVAL;
	}

	ret = imx_sc_misc_set_control(ipc_handle, IMX_SC_R_ENET_1,
				      IMX_SC_C_INTF_SEL, val >> 16);
	ret |= imx_sc_misc_set_control(ipc_handle, IMX_SC_R_ENET_1,
				       IMX_SC_C_CLK_GEN_EN, 0x1);
#endif

	return ret;
}

static int
imx8mp_set_stop_mode(struct plat_stmmacenet_data *plat_dat, bool is_en)
{
	/* TBD */
	return 0;
};

static int
imx8dxl_set_stop_mode(struct plat_stmmacenet_data *plat_dat, bool is_en)
{
	/* TBD */
	return 0;
};

static int imx_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct imx_priv_data *dwmac = priv;
	struct plat_stmmacenet_data *plat_dat = dwmac->plat_dat;
	int ret;

	if (dwmac->phy_supply) {
		ret = regulator_enable(dwmac->phy_supply);
		if (ret) {
			dev_err(&pdev->dev,
				"Failed to enable 'phy-supply' regulator: %d\n",
				ret);
			return ret;
		}
	}

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		goto disable_phy_supply;

	ret = clk_prepare_enable(dwmac->clk_mem);
	if (ret) {
		dev_err(&pdev->dev, "mem clock enable failed\n");
		goto clk_mem_failed;
	}

	ret = clk_prepare_enable(dwmac->clk_tx);
	if (ret) {
		dev_err(&pdev->dev, "tx clock enable failed\n");
		goto clk_tx_en_failed;
	}

	if (dwmac->ops->set_stop_mode) {
		ret = dwmac->ops->set_stop_mode(plat_dat, false);
		if (ret)
			goto stop_mode_failed;
	}

	if (dwmac->ops->set_intf_mode) {
		ret = dwmac->ops->set_intf_mode(plat_dat);
		if (ret)
			goto intf_mode_failed;
	}

	return 0;

intf_mode_failed:
stop_mode_failed:
	clk_disable_unprepare(dwmac->clk_tx);
clk_tx_en_failed:
	clk_disable_unprepare(dwmac->clk_mem);
clk_mem_failed:
	pm_runtime_put_noidle(&pdev->dev);
disable_phy_supply:
	if (dwmac->phy_supply)
		regulator_disable(dwmac->phy_supply);
	return ret;
}

static void imx_dwmac_exit(struct platform_device *pdev, void *priv)
{
	struct imx_priv_data *dwmac = priv;
	struct plat_stmmacenet_data *plat_dat = dwmac->plat_dat;
	int ret;

	if (dwmac->ops->set_stop_mode) {
		ret = dwmac->ops->set_stop_mode(plat_dat, true);
		if (ret) {
			dev_err(dwmac->dev, "enter stop mode failed %d\n", ret);
			return;
		}
	}

	if (dwmac->clk_tx)
		clk_disable_unprepare(dwmac->clk_tx);
	clk_disable_unprepare(dwmac->clk_mem);
	pm_runtime_put(&pdev->dev);
	if (dwmac->phy_supply)
		regulator_disable(dwmac->phy_supply);
}

static void imx_dwmac_fix_speed(void *priv, unsigned int speed)
{
	struct imx_priv_data *dwmac = priv;
	struct plat_stmmacenet_data *plat_dat = dwmac->plat_dat;
	unsigned long rate;
	int err;

	if (dwmac->ops->mac_txclk_auto_adj ||
	    (plat_dat->interface == PHY_INTERFACE_MODE_RMII) ||
	    (plat_dat->interface == PHY_INTERFACE_MODE_MII))
		return;

	switch (speed) {
	case SPEED_1000:
		rate = 125000000;
		break;
	case SPEED_100:
		rate = 25000000;
		break;
	case SPEED_10:
		rate = 2500000;
		break;
	default:
		dev_err(dwmac->dev, "invalid speed %u\n", speed);
		return;
	}

	err = clk_set_rate(dwmac->clk_tx, rate);
	if (err < 0)
		dev_err(dwmac->dev, "failed to set tx rate %lu\n", rate);
}

static int
imx_dwmac_parse_dt(struct imx_priv_data *dwmac, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int err = 0;

	if (of_get_property(np, "snps,rmii_refclk_ext", NULL))
		dwmac->rmii_refclk_ext = true;

	dwmac->clk_tx = devm_clk_get(dev, "tx");
	if (IS_ERR(dwmac->clk_tx)) {
		dev_err(dev, "failed to get tx clock\n");
		return PTR_ERR(dwmac->clk_tx);
	}

	dwmac->clk_mem = NULL;
	if (of_machine_is_compatible("fsl,imx8dxl")) {
		dwmac->clk_mem = devm_clk_get(dev, "mem");
		if (IS_ERR(dwmac->clk_mem)) {
			dev_err(dev, "failed to get mem clock\n");
			return PTR_ERR(dwmac->clk_mem);
		}
	}

	if (of_machine_is_compatible("fsl,imx8mp")) {
		/* Binding doc describes the propety:
		   is required by i.MX8MP.
		   is optinoal for i.MX8DXL.
		 */
		dwmac->intf_regmap = syscon_regmap_lookup_by_phandle(np, "intf_mode");
		if (IS_ERR(dwmac->intf_regmap))
			return PTR_ERR(dwmac->intf_regmap);

		err = of_property_read_u32_index(np, "intf_mode", 1, &dwmac->intf_reg_off);
		if (err) {
			dev_err(dev, "Can't get intf mode reg offset (%d)\n", err);
			return err;
		}
	}

	dwmac->phy_supply = devm_regulator_get_optional(dev, "phy");
	if (IS_ERR(dwmac->phy_supply)) {
		if (PTR_ERR(dwmac->phy_supply) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_info(dev, "no phy-supply regulator found: %ld\n",
			 PTR_ERR(dwmac->phy_supply));
		dwmac->phy_supply = NULL;
	}

	return err;
}

static int imx_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct imx_priv_data *dwmac;
	const struct imx_dwmac_ops *data;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "failed to get match data\n");
		ret = -EINVAL;
		goto err_match_data;
	}

	dwmac->ops = data;
	dwmac->dev = &pdev->dev;

	ret = imx_dwmac_parse_dt(dwmac, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to parse OF data\n");
		goto err_parse_dt;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(dwmac->ops->addr_width));
	if (ret) {
		dev_err(&pdev->dev, "DMA mask set failed\n");
		goto err_dma_mask;
	}

	plat_dat->init = imx_dwmac_init;
	plat_dat->exit = imx_dwmac_exit;
	plat_dat->fix_mac_speed = imx_dwmac_fix_speed;
	plat_dat->bsp_priv = dwmac;
	dwmac->plat_dat = plat_dat;

	/* enable runtime pm to turn off power domain when netif down */
	pm_runtime_enable(&pdev->dev);

	ret = imx_dwmac_init(pdev, dwmac);
	if (ret)
		goto err_dwmac_init;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_drv_probe;

	return 0;

err_drv_probe:
	imx_dwmac_exit(pdev, plat_dat->bsp_priv);
err_dwmac_init:
	pm_runtime_disable(&pdev->dev);
err_dma_mask:
err_parse_dt:
err_match_data:
	stmmac_remove_config_dt(pdev, plat_dat);
	return ret;
}

int imx_dwmac_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return stmmac_pltfr_remove(pdev);
}

static struct imx_dwmac_ops imx8mp_dwmac_data = {
	.addr_width = 34,
	.mac_txclk_auto_adj = false,
	.set_intf_mode = imx8mp_set_intf_mode,
	.set_stop_mode = imx8mp_set_stop_mode,
};

static struct imx_dwmac_ops imx8dxl_dwmac_data = {
	.addr_width = 32, /* for bringup */
	.mac_txclk_auto_adj = true,
	.set_intf_mode = imx8dxl_set_intf_mode,
	.set_stop_mode = imx8dxl_set_stop_mode,
};

static const struct of_device_id imx_dwmac_match[] = {
	{ .compatible = "nxp,imx8mp-dwmac-eqos", .data = &imx8mp_dwmac_data },
	{ .compatible = "nxp,imx8dxl-dwmac-eqos", .data = &imx8dxl_dwmac_data },
	{ }
};
MODULE_DEVICE_TABLE(of, imx_dwmac_match);

static struct platform_driver imx_dwmac_driver = {
	.probe  = imx_dwmac_probe,
	.remove = imx_dwmac_remove,
	.driver = {
		.name		= "imx-dwmac",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = imx_dwmac_match,
	},
};
module_platform_driver(imx_dwmac_driver);

MODULE_AUTHOR("NXP");
MODULE_DESCRIPTION("NXP imx8 DWMAC Specific Glue layer");
MODULE_LICENSE("GPL v2");
