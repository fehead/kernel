/*
 * Copyright (C) 2012 Thomas Petazzoni
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _IRQCHIP_H
#define _IRQCHIP_H

#include <linux/of.h>

/*
 * This macro must be used by the different irqchip drivers to declare
 * the association between their DT compatible string and their
 * initialization function.
 *
 * @name: name that must be unique accross all IRQCHIP_DECLARE of the
 * same file.
 * @compstr: compatible string of the irqchip driver
 * @fn: initialization function
 */
/* IAMROOT-12 fehead (2017-04-15):
 * --------------------------
 * OF_DECLARE_2(irqchip, name, compat, fn)
 *	_OF_DECLARE(irqchip, name, compat, fn, of_init_fn_2)
 *		static const struct of_device_id __of_table_##name
 *			__used __section(__irqchip_of_table)
 *			 = { .compatible = compat,				\
 *			     .data = fn }
 * ex) IRQCHIP_DECLARE(gic_400, "arm,gic-400", gic_of_init);
 *	static const struct of_device_id __of_table_gic_400
 *		__used __section(__irqchip_of_table)
 *		 = { .compatible = "arm,gic-400",
 *		     .data = gic_of_init }
 */
#define IRQCHIP_DECLARE(name, compat, fn) OF_DECLARE_2(irqchip, name, compat, fn)

#endif
