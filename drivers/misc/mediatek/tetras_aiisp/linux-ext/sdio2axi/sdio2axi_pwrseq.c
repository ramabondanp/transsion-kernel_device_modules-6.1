/*
 * (C) Copyright 2021-2023, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Change Logs:
 * Date           Author         Notes
 * 2023-4-20     yanghua     Initialize.
 */

/**
 * @brief   sdiobridge pwrseq driver
 * @date    2023-06-03
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_sdio2axi {
	struct device *dev;
	struct mmc_pwrseq pwrseq;
};

#define to_pwrseq_sdio2axi(p) container_of(p, struct mmc_pwrseq_sdio2axi, pwrseq)

static void mmc_pre_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_sdio2axi *pwrseq = to_pwrseq_sdio2axi(host->pwrseq);

	dev_info(pwrseq->dev, "sdio2axi pre power on\n");
}

static void mmc_post_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_sdio2axi *pwrseq = to_pwrseq_sdio2axi(host->pwrseq);

	dev_info(pwrseq->dev, "sdio2axi post power on\n");
}

static void mmc_power_off(struct mmc_host *host)
{
	struct mmc_pwrseq_sdio2axi *pwrseq = to_pwrseq_sdio2axi(host->pwrseq);

	dev_info(pwrseq->dev, "sdio2axi power off\n");
}

static const struct mmc_pwrseq_ops mmc_pwrseq_ops = {
	.pre_power_on = mmc_pre_power_on,
	.post_power_on = mmc_post_power_on,
	.power_off = mmc_power_off,
};

static const struct of_device_id mmc_pwrseq_of_sdio2axi_match[] = {
	{ .compatible = "sdio2axi,pwrseq",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, mmc_pwrseq_of_sdio2axi_match);

static int mmc_pwrseq_sdio2axi_probe(struct platform_device *pdev)
{
	struct mmc_pwrseq_sdio2axi *sdio2axi_pwrseq;
	struct device *dev = &pdev->dev;

	sdio2axi_pwrseq = devm_kzalloc(dev, sizeof(*sdio2axi_pwrseq), GFP_KERNEL);
	if (!sdio2axi_pwrseq)
		return -ENOMEM;

	sdio2axi_pwrseq->pwrseq.dev = dev;
	sdio2axi_pwrseq->pwrseq.ops = &mmc_pwrseq_ops;
	sdio2axi_pwrseq->pwrseq.owner = THIS_MODULE;
	sdio2axi_pwrseq->dev = dev;
	platform_set_drvdata(pdev, sdio2axi_pwrseq);

	return mmc_pwrseq_register(&sdio2axi_pwrseq->pwrseq);
}

static int mmc_pwrseq_sdio2axi_remove(struct platform_device *pdev)
{
	struct mmc_pwrseq_sdio2axi *sdio2axi_pwrseq = platform_get_drvdata(pdev);

	mmc_pwrseq_unregister(&sdio2axi_pwrseq->pwrseq);

	return 0;
}

struct platform_driver mmc_pwrseq_sdio2axi_driver = {
	.probe = mmc_pwrseq_sdio2axi_probe,
	.remove = mmc_pwrseq_sdio2axi_remove,
	.driver = {
		.name = "pwrseq_sdio2axi",
		.of_match_table = mmc_pwrseq_of_sdio2axi_match,
	},
};
