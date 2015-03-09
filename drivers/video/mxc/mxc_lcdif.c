/*
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/ipu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mxcfb.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include "mxc_dispdrv.h"

struct mxc_lcd_platform_data {
	u32 default_ifmt;
	u32 ipu_id;
	u32 disp_id;
	enum display_flags disp_flags;
};

struct mxc_lcdif_data {
	struct device *dev;
	struct mxc_dispdrv_handle *disp_lcdif;
};

#define DISPDRV_LCD	"lcd"

static struct fb_videomode lcdif_modedb[] = {
	{
		/* 800x480 @ 57 Hz , pixel clk @ 27MHz */
		"CLAA-WVGA", 57, 800, 480, 37037, 40, 60, 10, 10, 20, 10,
		FB_SYNC_CLK_LAT_FALL,
		FB_VMODE_NONINTERLACED,
		0,
	},
	{
		/* 800x480 @ 60 Hz , pixel clk @ 32MHz */
		"SEIKO-WVGA", 60, 800, 480, 29850, 89, 164, 23, 10, 10, 10,
		FB_SYNC_CLK_LAT_FALL,
		FB_VMODE_NONINTERLACED,
		0,
	},
	{
		.name = "VGA",
		.pixclock = KHZ2PICOS(25200),
		.xres = 640,
		.yres = 480,
		.left_margin = 48,
		.hsync_len = 96,
		.right_margin = 16,
		.upper_margin = 31,
		.vsync_len = 2,
		.lower_margin = 12,
	},
	{
		.name = "ETV570",
		.pixclock = KHZ2PICOS(25200),
		.xres = 640,
		.yres = 480,
		.left_margin = 114,
		.hsync_len = 30,
		.right_margin = 16,
		.upper_margin = 32,
		.vsync_len = 3,
		.lower_margin = 10,
	},
	{
		.name = "ET0350",
		.pixclock = KHZ2PICOS(6413760 / 1000),
		.xres = 320,
		.yres = 240,
		.left_margin = 34,
		.hsync_len = 34,
		.right_margin = 20,
		.upper_margin = 15,
		.vsync_len = 3,
		.lower_margin = 4,
	},
	{
		.name = "ET0430",
		.pixclock = KHZ2PICOS(9009),
		.xres = 480,
		.yres = 272,
		.left_margin = 2,
		.hsync_len = 41,
		.right_margin = 2,
		.upper_margin = 2,
		.vsync_len = 10,
		.lower_margin = 2,
		.sync = FB_SYNC_CLK_LAT_FALL,
	},
	{
		.name = "ET0500",
		.pixclock = KHZ2PICOS(33264),
		.xres = 800,
		.yres = 480,
		.left_margin = 88,
		.hsync_len = 128,
		.right_margin = 40,
		.upper_margin = 33,
		.vsync_len = 2,
		.lower_margin = 10,
	},
	{
		.name = "ET0700", /* same as ET0500 */
		.pixclock = KHZ2PICOS(33264),
		.xres = 800,
		.yres = 480,
		.left_margin = 88,
		.hsync_len = 128,
		.right_margin = 40,
		.upper_margin = 33,
		.vsync_len = 2,
		.lower_margin = 10,
	},
	{
		.name = "ETQ570",
		.pixclock = KHZ2PICOS(6596040 / 1000),
		.xres = 320,
		.yres = 240,
		.left_margin = 38,
		.hsync_len = 30,
		.right_margin = 30,
		.upper_margin = 16,
		.vsync_len = 3,
		.lower_margin = 4,
	},
};
static int lcdif_modedb_sz = ARRAY_SIZE(lcdif_modedb);

static int lcdif_init(struct mxc_dispdrv_handle *disp,
	struct mxc_dispdrv_setting *setting)
{
	int ret, i;
	struct mxc_lcdif_data *lcdif = mxc_dispdrv_getdata(disp);
	struct mxc_lcd_platform_data *plat_data = lcdif->dev->platform_data;
	struct fb_videomode *modedb = lcdif_modedb;
	int modedb_sz = lcdif_modedb_sz;

	/* use platform defined ipu/di */
	setting->dev_id = plat_data->ipu_id;
	setting->disp_id = plat_data->disp_id;

	if (setting->fbmode) {
		modedb[0] = *setting->fbmode;
		modedb_sz = 1;
	} else {
		setting->disp_flags = plat_data->disp_flags;
	}

	ret = fb_find_mode(&setting->fbi->var, setting->fbi, setting->dft_mode_str,
				modedb, modedb_sz, NULL, setting->default_bpp);
	if (!ret) {
		fb_videomode_to_var(&setting->fbi->var, &modedb[0]);
		setting->if_fmt = plat_data->default_ifmt;
	}

	INIT_LIST_HEAD(&setting->fbi->modelist);
	for (i = 0; i < modedb_sz; i++) {
		struct fb_videomode m;
		fb_var_to_videomode(&m, &setting->fbi->var);
		if (fb_mode_is_equal(&m, &modedb[i])) {
			fb_add_videomode(&modedb[i],
					&setting->fbi->modelist);
			break;
		}
	}

	return ret;
}

void lcdif_deinit(struct mxc_dispdrv_handle *disp)
{
	/*TODO*/
}

static struct mxc_dispdrv_driver lcdif_drv = {
	.name	= DISPDRV_LCD,
	.init	= lcdif_init,
	.deinit	= lcdif_deinit,
};

static int lcd_get_of_property(struct device *dev,
				struct mxc_lcd_platform_data *plat_data)
{
	struct device_node *np = dev->of_node;
	int err;
	u32 ipu_id, disp_id;
	const char *default_ifmt;

	err = of_property_read_string(np, "default_ifmt", &default_ifmt);
	if (err) {
		dev_err(dev, "get of property default_ifmt fail\n");
		return err;
	}
	err = of_property_read_u32(np, "ipu_id", &ipu_id);
	if (err) {
		dev_err(dev, "get of property ipu_id fail\n");
		return err;
	}
	err = of_property_read_u32(np, "disp_id", &disp_id);
	if (err) {
		dev_err(dev, "get of property disp_id fail\n");
		return err;
	}

	plat_data->ipu_id = ipu_id;
	plat_data->disp_id = disp_id;
	if (!strncmp(default_ifmt, "RGB24", 5))
		plat_data->default_ifmt = IPU_PIX_FMT_RGB24;
	else if (!strncmp(default_ifmt, "BGR24", 5))
		plat_data->default_ifmt = IPU_PIX_FMT_BGR24;
	else if (!strncmp(default_ifmt, "GBR24", 5))
		plat_data->default_ifmt = IPU_PIX_FMT_GBR24;
	else if (!strncmp(default_ifmt, "RGB565", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_RGB565;
	else if (!strncmp(default_ifmt, "RGB666", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_RGB666;
	else if (!strncmp(default_ifmt, "YUV444", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_YUV444;
	else if (!strncmp(default_ifmt, "LVDS666", 7))
		plat_data->default_ifmt = IPU_PIX_FMT_LVDS666;
	else if (!strncmp(default_ifmt, "YUYV16", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_YUYV;
	else if (!strncmp(default_ifmt, "UYVY16", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_UYVY;
	else if (!strncmp(default_ifmt, "YVYU16", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_YVYU;
	else if (!strncmp(default_ifmt, "VYUY16", 6))
				plat_data->default_ifmt = IPU_PIX_FMT_VYUY;
	else {
		dev_err(dev, "err default_ifmt!\n");
		return -ENOENT;
	}

	return err;
}

static int mxc_lcdif_probe(struct platform_device *pdev)
{
	int ret;
	struct mxc_lcdif_data *lcdif;
	struct mxc_lcd_platform_data *plat_data;

	dev_dbg(&pdev->dev, "%s enter\n", __func__);
	lcdif = devm_kzalloc(&pdev->dev, sizeof(struct mxc_lcdif_data),
				GFP_KERNEL);
	if (!lcdif)
		return -ENOMEM;
	plat_data = devm_kzalloc(&pdev->dev,
				sizeof(struct mxc_lcd_platform_data),
				GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;
	pdev->dev.platform_data = plat_data;

	ret = lcd_get_of_property(&pdev->dev, plat_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "get lcd of property fail\n");
		return ret;
	}

	lcdif->dev = &pdev->dev;
	lcdif->disp_lcdif = mxc_dispdrv_register(&lcdif_drv);
	mxc_dispdrv_setdata(lcdif->disp_lcdif, lcdif);

	dev_set_drvdata(&pdev->dev, lcdif);
	dev_dbg(&pdev->dev, "%s exit\n", __func__);

	return ret;
}

static int mxc_lcdif_remove(struct platform_device *pdev)
{
	struct mxc_lcdif_data *lcdif = platform_get_drvdata(pdev);

	mxc_dispdrv_puthandle(lcdif->disp_lcdif);
	mxc_dispdrv_unregister(lcdif->disp_lcdif);
	kfree(lcdif);
	return 0;
}

static const struct of_device_id imx_lcd_dt_ids[] = {
	{ .compatible = "fsl,lcd"},
	{ /* sentinel */ }
};
static struct platform_driver mxc_lcdif_driver = {
	.driver = {
		.name = "mxc_lcdif",
		.of_match_table	= imx_lcd_dt_ids,
	},
	.probe = mxc_lcdif_probe,
	.remove = mxc_lcdif_remove,
};

static int __init mxc_lcdif_init(void)
{
	return platform_driver_register(&mxc_lcdif_driver);
}

static void __exit mxc_lcdif_exit(void)
{
	platform_driver_unregister(&mxc_lcdif_driver);
}

module_init(mxc_lcdif_init);
module_exit(mxc_lcdif_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("i.MX ipuv3 LCD extern port driver");
MODULE_LICENSE("GPL");
