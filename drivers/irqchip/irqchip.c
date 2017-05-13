/*
 * Copyright (C) 2012 Thomas Petazzoni
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/irqchip.h>

/*
 * This special of_device_id is the sentinel at the end of the
 * of_device_id[] array of all irqchips. It is automatically placed at
 * the end of the array by the linker, thanks to being part of a
 * special section.
 */
static const struct of_device_id
irqchip_of_match_end __used __section(__irqchip_of_table_end);

/* IAMROOT-12 fehead (2017-04-01):
 * --------------------------
 * IRQCHIP_DECLARE 참고
 */
/*
 * ex) IRQCHIP_DECLARE(gic_400, "arm,gic-400", gic_of_init);
 *	static const struct of_device_id __of_table_gic_400
 *		__used __section(__irqchip_of_table)
 *		 = { .compatible = "arm,gic-400",
 *		     .data = gic_of_init }
 */
extern struct of_device_id __irqchip_of_table[];

void __init irqchip_init(void)
{
	of_irq_init(__irqchip_of_table);
}
