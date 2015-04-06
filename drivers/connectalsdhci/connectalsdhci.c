/*
 * Xilinx Zynq Secure Digital Host Controller Interface.
 * Copyright (C) 2011 - 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2012 Wind River Systems, Inc.
 *
 * Based on sdhci-of-esdhc.c
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include "sdhci-pltfm.h"

/**
 * struct xsdhcips
 * @devclk		Pointer to the peripheral clock
 * @aperclk		Pointer to the APER clock
 * @clk_rate_change_nb	Notifier block for clock frequency change callback
 */
struct xsdhcips {
	struct clk		*devclk;
	struct clk		*aperclk;
	struct notifier_block	clk_rate_change_nb;
};

static unsigned int zynq_of_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return pltfm_host->clock;
}

static struct sdhci_ops sdhci_zynq_ops = {
	.get_max_clock = zynq_of_get_max_clock,
};

static struct sdhci_pltfm_data sdhci_zynq_pdata = {
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK,
	.ops = &sdhci_zynq_ops,
};

static int xsdhcips_clk_notifier_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	switch (event) {
	case PRE_RATE_CHANGE:
		/* if a rate change is announced we need to check whether we can
		 * maintain the current frequency by changing the clock
		 * dividers. And we may have to suspend operation and return
		 * after the rate change or its abort
		 */
		/* fall through */
	case POST_RATE_CHANGE:
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

#ifdef CONFIG_PM_SLEEP
/**
 * xsdhcips_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Put the device in a low power state.
 */
static int xsdhcips_suspend(struct device *dev)
{
  printk("[%s:%d]\n", __FUNCTION__, __LINE__);
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xsdhcips *xsdhcips = pltfm_host->priv;
	int ret;

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	clk_disable(xsdhcips->devclk);
	clk_disable(xsdhcips->aperclk);

	return 0;
}

/**
 * xsdhcips_resume - Resume method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Resume operation after suspend
 */
static int xsdhcips_resume(struct device *dev)
{
  printk("[%s:%d]\n", __FUNCTION__, __LINE__);
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xsdhcips *xsdhcips = pltfm_host->priv;
	int ret;

	ret = clk_enable(xsdhcips->aperclk);
	if (ret) {
		dev_err(dev, "Cannot enable APER clock.\n");
		return ret;
	}

	ret = clk_enable(xsdhcips->devclk);
	if (ret) {
		dev_err(dev, "Cannot enable device clock.\n");
		clk_disable(xsdhcips->aperclk);
		return ret;
	}

	return sdhci_resume_host(host);
}

static const struct dev_pm_ops xsdhcips_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xsdhcips_suspend, xsdhcips_resume)
};
#define XSDHCIPS_PM	(&xsdhcips_dev_pm_ops)

#else /* ! CONFIG_PM_SLEEP */
#define XSDHCIPS_PM	NULL
#endif /* ! CONFIG_PM_SLEEP */


static void print_present_state(){
 // p1549
  int t;
  for (t = 0; t < 2; t++){
    void *foo = ioremap_nocache(0xE0100024+(t*0x1000), sizeof(u32));
    u32 v = readl(foo);
    printk("SDIO (%d) present_state: %08x\n", t, v);
    //printk("SDIO (%d) present_state: %d %x %d %d %d %d\n", t, (v>>24)&0x1, (v>>20)&0xF, (v>>19)&0x1, (v>>18)&0x1, (v>>17)&0x1, (v>>16)&0x1);
  }
}

//extern void set_mdk_calls_reset(unsigned int v);

static int sdhci_zynq_probe(struct platform_device *pdev)
{

  printk("[%s:%d]\n", __FUNCTION__, __LINE__);
  //set_mdk_calls_reset(1);  


  void * foo = ioremap_nocache(0xF8000150, sizeof(u32));
  u32 v = readl(foo);
  printk("SDIO_CLK_CTRL: %08x\n", v);
  //writel(v | 0x2, foo);
  v = readl(foo);
  printk("SDIO_CLK_CTRL: %08x\n", v);

  /* int t; */

  /*   // p1644 */
  /* void *foo = ioremap_nocache(0xF8000700, 54*sizeof(u32)); */
  /* int brk = 0; */
  /* for (t = 0; t < 53; t++){ */
  /*   if ((t >= 9 && t <= 15) || (t >= 22 && t <= 27) || (t >= 34 && t <= 39) || (t >= 40 && t <= 47) || (t >= 46 && t <= 51)){ */
  /*     u32 v = readl(foo); */
  /*     /\* v &= (0xFFFFFFFF ^ (0x7F<<1)); *\/ */
  /*     /\* switch(t){ *\/ */
  /*     /\* case 9: *\/ */
  /*     /\* 	v |= 3<<3; *\/ */
  /*     /\* 	break; *\/ */
  /*     /\* default: *\/ */
  /*     /\* 	v |= 4<<5; *\/ */
  /*     /\* 	break; *\/ */
  /*     /\* } *\/ */
  /*     /\* writel(v, foo); *\/ */
  /*     v = readl(foo); */
  /*     printk("mio %2d: (%08x)  %d %d %d %d\n", t, foo, (v>>5)&0x7, (v>>3)&0x3, (v>>2)&0x1, (v>>1)&0x1); */
  /*     brk = 1; */
  /*   } else if (brk){ */
  /*     printk("\n"); */
  /*     brk = 0; */
  /*   } */
  /*   foo += sizeof(u32); */
  /* } */


  /* // p1702 */
  /* foo = ioremap_nocache(0xF8000830, 2*sizeof(u32)); */
  /* for (t = 0; t < 2; t++){ */
  /*   u32 v = readl(foo+(t*sizeof(u32))); */
  /*   printk("SD%d_WP_CD_SEL %d %d\n", t, (v>>16)&0x3F, (v>>0)&0x3F); */
  /* } */

  /* printk("[%s:%d]\n", __FUNCTION__, __LINE__); */
  /* print_present_state(); */


	int ret;
	int irq = platform_get_irq(pdev, 0);
	const void *prop;
	struct device_node *np = pdev->dev.of_node;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct xsdhcips *xsdhcips;

	xsdhcips = kmalloc(sizeof(*xsdhcips), GFP_KERNEL);
	if (!xsdhcips) {
		dev_err(&pdev->dev, "unable to allocate memory\n");
		return -ENOMEM;
	}

	if (irq == 56)
		xsdhcips->aperclk = clk_get_sys("SDIO0_APER", NULL);
	else
		xsdhcips->aperclk = clk_get_sys("SDIO1_APER", NULL);

	if (IS_ERR(xsdhcips->aperclk)) {
		dev_err(&pdev->dev, "APER clock not found.\n");
		ret = PTR_ERR(xsdhcips->aperclk);
		goto err_free;
	}

	if (irq == 56)
		xsdhcips->devclk = clk_get_sys("SDIO0", NULL);
	else
		xsdhcips->devclk = clk_get_sys("SDIO1", NULL);

	if (IS_ERR(xsdhcips->devclk)) {
		dev_err(&pdev->dev, "Device clock not found.\n");
		ret = PTR_ERR(xsdhcips->devclk);
		goto clk_put_aper;
	}

	ret = clk_prepare_enable(xsdhcips->aperclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable APER clock.\n");
		goto clk_put;
	}

	ret = clk_prepare_enable(xsdhcips->devclk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable device clock.\n");
		goto clk_dis_aper;
	}

	xsdhcips->clk_rate_change_nb.notifier_call = xsdhcips_clk_notifier_cb;
	xsdhcips->clk_rate_change_nb.next = NULL;
	if (clk_notifier_register(xsdhcips->devclk,
				&xsdhcips->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");

	ret = sdhci_pltfm_register(pdev, &sdhci_zynq_pdata);
	if (ret) {
		dev_err(&pdev->dev, "Platform registration failed\n");
		goto clk_notif_unreg;
	}

	host = platform_get_drvdata(pdev);
	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = xsdhcips;


	prop = of_get_property(np, "xlnx,has-cd", NULL);
	if (prop == NULL || (!(u32) be32_to_cpup(prop)))
		host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;

  /* printk("[%s:%d]\n", __FUNCTION__, __LINE__); */
  /* print_present_state(); */
	//set_mdk_calls_reset(0);
	v = readl(foo);
	printk("SDIO_CLK_CTRL: %08x\n", v);
	printk("[%s:%d]\n", __FUNCTION__, __LINE__);

	return 0;

clk_notif_unreg:
	clk_notifier_unregister(xsdhcips->devclk,
			&xsdhcips->clk_rate_change_nb);
	clk_disable_unprepare(xsdhcips->devclk);
clk_dis_aper:
	clk_disable_unprepare(xsdhcips->aperclk);
clk_put:
	clk_put(xsdhcips->devclk);
clk_put_aper:
	clk_put(xsdhcips->aperclk);
err_free:
	kfree(xsdhcips);

	//set_mdk_calls_reset(0);
	return ret;
}

static int sdhci_zynq_remove(struct platform_device *pdev)
{
  printk("[%s:%d]\n", __FUNCTION__, __LINE__);

	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xsdhcips *xsdhcips = pltfm_host->priv;

	clk_notifier_unregister(xsdhcips->devclk,
			&xsdhcips->clk_rate_change_nb);
	clk_disable_unprepare(xsdhcips->devclk);
	clk_disable_unprepare(xsdhcips->aperclk);
	clk_put(xsdhcips->devclk);
	clk_put(xsdhcips->aperclk);
	kfree(xsdhcips);

	return sdhci_pltfm_unregister(pdev);
}

static const struct of_device_id sdhci_zynq_of_match[] = {
	{ .compatible = "connectalsdhci" },
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_zynq_of_match);

static struct platform_driver sdhci_zynq_driver = {
	.driver = {
		.name = "connectalsdhci-zynq",
		.owner = THIS_MODULE,
		.of_match_table = sdhci_zynq_of_match,
		.pm = XSDHCIPS_PM,
	},
	.probe = sdhci_zynq_probe,
	.remove = sdhci_zynq_remove,
};

module_platform_driver(sdhci_zynq_driver);

MODULE_DESCRIPTION("Secure Digital Host Controller Interface OF driver");
MODULE_AUTHOR("Michal Simek <monstr@monstr.eu>, Vlad Lungu <vlad.lungu@windriver.com>");
MODULE_LICENSE("GPL v2");
