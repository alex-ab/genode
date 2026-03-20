/*
 * \brief  Replaces drivers/clk/clkdev.c
 * \author Stefan Kalkowski
 * \date   2021-03-16
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <linux/clkdev.h>
#include <linux/device.h>
#include <lx_emul/clock.h>

struct clk *clk_get(struct device *dev, const char *con_id)
{
	struct clk * ret;

	printk("%s:%d dev: %px dev->of_node: %px\n", __func__, __LINE__, dev, dev->of_node);
	if (!dev || !dev->of_node)
		return NULL;

	printk("%s:%d dev: %px dev->of_node: %px\n", __func__, __LINE__, dev, dev->of_node);

	ret = lx_emul_clock_get(dev->of_node, con_id);
	return ret ? ret : ERR_PTR(-ENOENT);
}


void clk_put(struct clk *clk) {}
