/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/of.h>
#include <linux/clocksource.h>

extern struct of_device_id __clksrc_of_table[];

static const struct of_device_id __clksrc_of_table_sentinel
	__used __section(__clksrc_of_table_end);

void __init clocksource_of_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	of_init_fn_1 init_func;
	unsigned clocksources = 0;

/* IAMROOT-12:
 * -------------
 * time_init()에서 호출되었다.
 */
	/* IAMROOT-12 fehead (2017-06-12):
	 * --------------------------
	 * bcm2709.dtsi 참고.
	 * np = timer {
	 *	compatible = "arm,armv7-timer";
	 * 	clock-frequency = <19200000>;
	 * 	interrupts = <3 0>, // PHYS_SECURE_PPI
	 *		<3 1>, // PHYS_NONSECURE_PPI
	 * 		<3 3>, // VIRT_PPI
	 * 		<3 2>; // HYP_PPI
	 * 	always-on;
	 * };
	 */
	for_each_matching_node_and_match(np, __clksrc_of_table, &match) {
		if (!of_device_is_available(np))
			continue;

		/* IAMROOT-12 fehead (2017-06-12):
		 * --------------------------
		 *  init_func = arch_timer_init()
		 */
		init_func = match->data;
		init_func(np);
		clocksources++;
	}
	if (!clocksources)
		pr_crit("%s: no matching clocksources found\n", __func__);
}
