/*
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file contains the code used to make IRQ descriptions in the
 * device tree to actual irq numbers on an interrupt controller
 * driver.
 */

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/string.h>
#include <linux/slab.h>

/**
 * irq_of_parse_and_map - Parse and map an interrupt into linux virq space
 * @dev: Device node of the device whose interrupt is to be mapped
 * @index: Index of the interrupt to map
 *
 * This function is a wrapper that chains of_irq_parse_one() and
 * irq_create_of_mapping() to make things easier to callers
 */
/* IAMROOT-12 fehead (2017-04-23):
 * --------------------------
 * 인터럽트를 구문 분석하여 Linux virq 공간에 매핑합니다.
 * 이 함수는 for_irq_parse_one()과 irq_create_of_mapping()을 묶어 호출자를 쉽게
 * 만들어주는 래퍼입니다.
 *
 * gic_of_init -> irq_of_parse_and_map(node, 0);
 */
unsigned int irq_of_parse_and_map(struct device_node *dev, int index)
{
	struct of_phandle_args oirq;

/* IAMROOT-12:
 * -------------
 * 사용할 인터럽트 정보를 파싱해온다.
 * (pci인 경우 매핑 포함)
 */
	if (of_irq_parse_one(dev, index, &oirq))
		return 0;

	return irq_create_of_mapping(&oirq);
}
EXPORT_SYMBOL_GPL(irq_of_parse_and_map);

/**
 * of_irq_find_parent - Given a device node, find its interrupt parent node
 * @child: pointer to device node
 *
 * Returns a pointer to the interrupt parent node, or NULL if the interrupt
 * parent could not be determined.
 */
struct device_node *of_irq_find_parent(struct device_node *child)
{
	struct device_node *p;
	const __be32 *parp;

	if (!of_node_get(child))
		return NULL;

/* IAMROOT-12:
 * -------------
 * 상위 인터럽트 컨트롤러를 알아온다.
 */
	do {
		parp = of_get_property(child, "interrupt-parent", NULL);
		if (parp == NULL)
			p = of_get_parent(child);
		else {
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				p = of_node_get(of_irq_dflt_pic);
			else

/* IAMROOT-12:
 * -------------
 * phandle 값으로 노드를 찾아온다. -> 부모 노드를 찾아온다.
 */
				p = of_find_node_by_phandle(be32_to_cpup(parp));
		}
		of_node_put(child);
		child = p;
	} while (p && of_get_property(p, "#interrupt-cells", NULL) == NULL);

	return p;
}

/**
 * of_irq_parse_raw - Low level interrupt tree parsing
 * @parent:	the device interrupt parent
 * @addr:	address specifier (start of "reg" property of the device) in be32 format
 * @out_irq:	structure of_irq updated by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent. It takes an interrupt specifier structure as
 * input, walks the tree looking for any interrupt-map properties, translates
 * the specifier for each map, and then returns the translated map.
 */
/* IAMROOT-12 fehead (2017-04-22):
 * --------------------------
 * 이 함수는 로우 레벨 인터럽트 트리 워킹 함수입니다. 예를 들어 부모를위한 장치
 * 노드가 없을 때 PCI 인터럽트를 해결할 때와 같이 합성 된 reg 및 인터럽트 속성을
 * 사용하여 부분적으로 살펴볼 수 있습니다. 인터럽트 지정자 구조를 입력으로 사용
 * 하고 트리를 탐색하여 인터럽트 맵 속성을 찾고 각 매핑에 대한 지정자를 변환 한
 * 다음 변환 된 맵을 반환합니다.
 */
/* IAMROOT-12:
 * -------------
 * pci 매핑도 계층형을 지원하여 매핑이 여러 번 될 수도 있다.
 */
int of_irq_parse_raw(const __be32 *addr, struct of_phandle_args *out_irq)
{
	struct device_node *ipar, *tnode, *old = NULL, *newpar = NULL;
	__be32 initial_match_array[MAX_PHANDLE_ARGS];
	const __be32 *match_array = initial_match_array;
	const __be32 *tmp, *imap, *imask, dummy_imask[] = { [0 ... MAX_PHANDLE_ARGS] = ~0 };
	u32 intsize = 1, addrsize, newintsize = 0, newaddrsize = 0;
	int imaplen, match, i;

#ifdef DEBUG
	of_print_phandle_args("of_irq_parse_raw: ", out_irq);
#endif

/* IAMROOT-12:
 * -------------
 * 부모 컨트롤러 디바이스 노드
 */
	ipar = of_node_get(out_irq->np);

	/* First get the #interrupt-cells property of the current cursor
	 * that tells us how to interpret the passed-in intspec. If there
	 * is none, we are nice and just walk up the tree
	 */

/* IAMROOT-12:
 * -------------
 * "#interrupt-cells"를 찾을 때까지 상위 인터럽트 컨트롤러를 찾는다.
 */
	do {
		tmp = of_get_property(ipar, "#interrupt-cells", NULL);
		if (tmp != NULL) {
			intsize = be32_to_cpu(*tmp);
			break;
		}
		tnode = ipar;
		ipar = of_irq_find_parent(ipar);
		of_node_put(tnode);
	} while (ipar);
	if (ipar == NULL) {
		pr_debug(" -> no parent found !\n");
		goto fail;
	}

	pr_debug("of_irq_parse_raw: ipar=%s, size=%d\n", of_node_full_name(ipar), intsize);

	if (out_irq->args_count != intsize)
		return -EINVAL;

	/* Look for this #address-cells. We have to implement the old linux
	 * trick of looking for the parent here as some device-trees rely on it
	 */

/* IAMROOT-12:
 * -------------
 * 부모 인터럽트 컨트롤러 노드에서 "#address-cells"를 찾아보고 없으면 
 * 상위 노드로 이동하며 찾는다.
 */
	old = of_node_get(ipar);
	do {
		tmp = of_get_property(old, "#address-cells", NULL);
		tnode = of_get_parent(old);
		of_node_put(old);
		old = tnode;
	} while (old && tmp == NULL);
	of_node_put(old);
	old = NULL;
	addrsize = (tmp == NULL) ? 2 : be32_to_cpu(*tmp);

	pr_debug(" -> addrsize=%d\n", addrsize);

	/* Range check so that the temporary buffer doesn't overflow */
	if (WARN_ON(addrsize + intsize > MAX_PHANDLE_ARGS))
		goto fail;

	/* Precalculate the match array - this simplifies match loop */

/* IAMROOT-12:
 * -------------
 * "reg =" 속성 값을 "#address-cells" 만큼 읽어와서 initial_match_array[]에 
 *	   저장한다.
 * args[]로 받은 값에서 "#interrupt-cells" 만큼 읽어와서 그 다음에 저장한다.
 *
 * initial_match_array[] <- {reg 값, args[]...}
 */
	for (i = 0; i < addrsize; i++)
		initial_match_array[i] = addr ? addr[i] : 0;
	for (i = 0; i < intsize; i++)
		initial_match_array[addrsize + i] = cpu_to_be32(out_irq->args[i]);

	/* Now start the actual "proper" walk of the interrupt tree */
	while (ipar != NULL) {
		/* Now check if cursor is an interrupt-controller and if it is
		 * then we are done
		 */

/* IAMROOT-12:
 * -------------
 * ipar(상위 인터럽트 컨트롤러라고 하는 곳) 노드에 "interrupt-controller"가 
 * 존재하면 실제 인터럽트 컨트롤러 이므로 매핑할 필요 없이 그냥 그대로 
 * out_irq를 수정하지 않고 사용하기 위해 함수를 빠져나간다.
 */
		if (of_get_property(ipar, "interrupt-controller", NULL) !=
				NULL) {
			pr_debug(" -> got it !\n");
			return 0;
		}

		/*
		 * interrupt-map parsing does not work without a reg
		 * property when #address-cells != 0
		 */
		if (addrsize && !addr) {
			pr_debug(" -> no reg passed in when needed !\n");
			goto fail;
		}

		/* Now look for an interrupt-map */

/* IAMROOT-12:
 * -------------
 * "interrupt-map" 속성을 읽어온다.
 */
		imap = of_get_property(ipar, "interrupt-map", &imaplen);
		/* No interrupt map, check for an interrupt parent */
		if (imap == NULL) {
			pr_debug(" -> no map, getting parent\n");
			newpar = of_irq_find_parent(ipar);
			goto skiplevel;
		}
		imaplen /= sizeof(u32);

		/* Look for a mask */

/* IAMROOT-12:
 * -------------
 * "interrupt-map-mask" 속성이 없는 경우 dummy_imask를 사용한다.
 * (더미 마스크는 인수 16개 모두 0xffff_ffff로 구성되어 있다.)
 */
		imask = of_get_property(ipar, "interrupt-map-mask", NULL);
		if (!imask)
			imask = dummy_imask;

		/* Parse interrupt-map */
		match = 0;
		while (imaplen > (addrsize + intsize + 1) && !match) {
			/* Compare specifiers */
			match = 1;

/* IAMROOT-12:
 * -------------
 * initial_match_array[]에 읽어온 데이터가 "interrupt-map"과 동일한 경우 
 * match가 0이 아니게 된다. 
 */
			for (i = 0; i < (addrsize + intsize); i++, imaplen--)
				match &= !((match_array[i] ^ *imap++) & imask[i]);

			pr_debug(" -> match=%d (imaplen=%d)\n", match, imaplen);

			/* Get the interrupt parent */
			if (of_irq_workarounds & OF_IMAP_NO_PHANDLE)
				newpar = of_node_get(of_irq_dflt_pic);
			else
				newpar = of_find_node_by_phandle(be32_to_cpup(imap));
			imap++;
			--imaplen;

			/* Check if not found */
			if (newpar == NULL) {
				pr_debug(" -> imap parent not found !\n");
				goto fail;
			}

			if (!of_device_is_available(newpar))
				match = 0;

			/* Get #interrupt-cells and #address-cells of new
			 * parent
			 */
			tmp = of_get_property(newpar, "#interrupt-cells", NULL);
			if (tmp == NULL) {
				pr_debug(" -> parent lacks #interrupt-cells!\n");
				goto fail;
			}
			newintsize = be32_to_cpu(*tmp);
			tmp = of_get_property(newpar, "#address-cells", NULL);
			newaddrsize = (tmp == NULL) ? 0 : be32_to_cpu(*tmp);

			pr_debug(" -> newintsize=%d, newaddrsize=%d\n",
			    newintsize, newaddrsize);

			/* Check for malformed properties */
			if (WARN_ON(newaddrsize + newintsize > MAX_PHANDLE_ARGS))
				goto fail;
			if (imaplen < (newaddrsize + newintsize))
				goto fail;

			imap += newaddrsize + newintsize;
			imaplen -= newaddrsize + newintsize;

			pr_debug(" -> imaplen=%d\n", imaplen);
		}
		if (!match)
			goto fail;

		/*
		 * Successfully parsed an interrrupt-map translation; copy new
		 * interrupt specifier into the out_irq structure
		 */

/* IAMROOT-12:
 * -------------
 * "interrupt-map"에서 지정한 인터럽트 컨트롤러와 인터럽트 argument를 out_irq에
 * 설정한다.
 */
		out_irq->np = newpar;

		match_array = imap - newaddrsize - newintsize;
		for (i = 0; i < newintsize; i++)
			out_irq->args[i] = be32_to_cpup(imap - newintsize + i);
		out_irq->args_count = intsize = newintsize;
		addrsize = newaddrsize;

	skiplevel:
		/* Iterate again with new parent */
		pr_debug(" -> new parent: %s\n", of_node_full_name(newpar));
		of_node_put(ipar);
		ipar = newpar;
		newpar = NULL;
	}
 fail:
	of_node_put(ipar);
	of_node_put(newpar);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(of_irq_parse_raw);

/**
 * of_irq_parse_one - Resolve an interrupt for a device
 * @device: the device whose interrupt is to be resolved
 * @index: index of the interrupt to resolve
 * @out_irq: structure of_irq filled by this function
 *
 * This function resolves an interrupt for a node by walking the interrupt tree,
 * finding which interrupt controller node it is attached to, and returning the
 * interrupt specifier that can be used to retrieve a Linux IRQ number.
 */
/* IAMROOT-12 fehead (2017-04-22):
 * --------------------------
 * 이 함수는 인터럽트 트리를 걷고 노드가 연결된 인터럽트 컨트롤러 노드를 찾은
 * 다음 Linux IRQ 번호를 검색하는 데 사용할 수있는 인터럽트 지정자를 반환하여
 * 노드의 인터럽트를 해결합니다.
 *
 * gic_of_init -> irq_of_parse_and_map(node, 0)
 *	->of_irq_parse_one(dev, 0, &oirq)
 */
int of_irq_parse_one(struct device_node *device, int index, struct of_phandle_args *out_irq)
{
	struct device_node *p;
	const __be32 *intspec, *tmp, *addr;
	u32 intsize, intlen;
	int i, res;

	pr_debug("of_irq_parse_one: dev=%s, index=%d\n", of_node_full_name(device), index);

	/* OldWorld mac stuff is "special", handle out of line */

/* IAMROOT-12:
 * -------------
 * powermac 워크어라운드 적용
 */
	if (of_irq_workarounds & OF_IMAP_OLDWORLD_MAC)
		return of_irq_parse_oldworld(device, index, out_irq);

	/* Get the reg property (if any) */
	addr = of_get_property(device, "reg", NULL);

	/* Try the new-style interrupts-extended first */

/* IAMROOT-12:
 * -------------
 * 신형: "interrupts=" 대신 "interrupts-extended"로 새로운 스타일을 사용한다.
 *       추가 인수로 컨트롤러를 지정한다. (첫 번째 인수=컨트롤러)
 */
	res = of_parse_phandle_with_args(device, "interrupts-extended",
					"#interrupt-cells", index, out_irq);

/* IAMROOT-12:
 * -------------
 * res 값이 0인 경우 성공하여 of_irq_parse_raw() 함수로 이동한다.
 */
	if (!res)
		return of_irq_parse_raw(addr, out_irq);

	/* Get the interrupts property */

/* IAMROOT-12:
 * -------------
 * 구형: "interrupts =" 속성은 "interrupt-parent =" 속성과 같이 사용된다.
 */
	intspec = of_get_property(device, "interrupts", &intlen);
	if (intspec == NULL)
		return -EINVAL;

	intlen /= sizeof(*intspec);

	pr_debug(" intspec=%d intlen=%d\n", be32_to_cpup(intspec), intlen);

	/* Look for the interrupt parent. */
	p = of_irq_find_parent(device);
	if (p == NULL)
		return -EINVAL;

	/* Get size of interrupt specifier */
	tmp = of_get_property(p, "#interrupt-cells", NULL);
	if (tmp == NULL) {
		res = -EINVAL;
		goto out;
	}
	intsize = be32_to_cpu(*tmp);

	pr_debug(" intsize=%d intlen=%d\n", intsize, intlen);

	/* Check index */
	if ((index + 1) * intsize > intlen) {
		res = -EINVAL;
		goto out;
	}

	/* Copy intspec into irq structure */
	intspec += index * intsize;
	out_irq->np = p;
	out_irq->args_count = intsize;
	for (i = 0; i < intsize; i++)
		out_irq->args[i] = be32_to_cpup(intspec++);

	/* Check if there are any interrupt-map translations to process */
	res = of_irq_parse_raw(addr, out_irq);
 out:
	of_node_put(p);
	return res;
}
EXPORT_SYMBOL_GPL(of_irq_parse_one);

/**
 * of_irq_to_resource - Decode a node's IRQ and return it as a resource
 * @dev: pointer to device tree node
 * @index: zero-based index of the irq
 * @r: pointer to resource structure to return result into.
 */
int of_irq_to_resource(struct device_node *dev, int index, struct resource *r)
{
	int irq = irq_of_parse_and_map(dev, index);

	/* Only dereference the resource if both the
	 * resource and the irq are valid. */
	if (r && irq) {
		const char *name = NULL;

		memset(r, 0, sizeof(*r));
		/*
		 * Get optional "interrupt-names" property to add a name
		 * to the resource.
		 */
		of_property_read_string_index(dev, "interrupt-names", index,
					      &name);

		r->start = r->end = irq;
		r->flags = IORESOURCE_IRQ | irqd_get_trigger_type(irq_get_irq_data(irq));
		r->name = name ? name : of_node_full_name(dev);
	}

	return irq;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource);

/**
 * of_irq_get - Decode a node's IRQ and return it as a Linux irq number
 * @dev: pointer to device tree node
 * @index: zero-based index of the irq
 *
 * Returns Linux irq number on success, or -EPROBE_DEFER if the irq domain
 * is not yet created.
 *
 */
int of_irq_get(struct device_node *dev, int index)
{
	int rc;
	struct of_phandle_args oirq;
	struct irq_domain *domain;

	rc = of_irq_parse_one(dev, index, &oirq);
	if (rc)
		return rc;

	domain = irq_find_host(oirq.np);
	if (!domain)
		return -EPROBE_DEFER;

	return irq_create_of_mapping(&oirq);
}
EXPORT_SYMBOL_GPL(of_irq_get);

/**
 * of_irq_get_byname - Decode a node's IRQ and return it as a Linux irq number
 * @dev: pointer to device tree node
 * @name: irq name
 *
 * Returns Linux irq number on success, or -EPROBE_DEFER if the irq domain
 * is not yet created, or error code in case of any other failure.
 */
int of_irq_get_byname(struct device_node *dev, const char *name)
{
	int index;

	if (unlikely(!name))
		return -EINVAL;

	index = of_property_match_string(dev, "interrupt-names", name);
	if (index < 0)
		return index;

	return of_irq_get(dev, index);
}

/**
 * of_irq_count - Count the number of IRQs a node uses
 * @dev: pointer to device tree node
 */
int of_irq_count(struct device_node *dev)
{
	struct of_phandle_args irq;
	int nr = 0;

	while (of_irq_parse_one(dev, nr, &irq) == 0)
		nr++;

	return nr;
}

/**
 * of_irq_to_resource_table - Fill in resource table with node's IRQ info
 * @dev: pointer to device tree node
 * @res: array of resources to fill in
 * @nr_irqs: the number of IRQs (and upper bound for num of @res elements)
 *
 * Returns the size of the filled in table (up to @nr_irqs).
 */
int of_irq_to_resource_table(struct device_node *dev, struct resource *res,
		int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++, res++)
		if (!of_irq_to_resource(dev, i, res))
			break;

	return i;
}
EXPORT_SYMBOL_GPL(of_irq_to_resource_table);

struct intc_desc {
	struct list_head	list;
	struct device_node	*dev;
	struct device_node	*interrupt_parent;
};

/**
 * of_irq_init - Scan and init matching interrupt controllers in DT
 * @matches: 0 terminated array of nodes to match and init function to call
 *
 * This function scans the device tree for matching interrupt controller nodes,
 * and calls their initialization functions in order with parents first.
 */

/* IAMROOT-12:
 * -------------
 * 디바이스 트리와 매치된 최상위 인터럽트 컨트롤러부터 초기화 함수를 호출한다.
 */
/* IAMROOT-12 fehead (2017-05-06):
 * --------------------------
 * of_irq_init(__irqchip_of_table);
 */
void __init of_irq_init(const struct of_device_id *matches)
{
	struct device_node *np, *parent = NULL;
	struct intc_desc *desc, *temp_desc;
	struct list_head intc_desc_list, intc_parent_list;

	INIT_LIST_HEAD(&intc_desc_list);
	INIT_LIST_HEAD(&intc_parent_list);

/* IAMROOT-12:
 * -------------
 * 현재 읽어온 디바이스 트리 노드들에서 matches와 같은 노드들을 대상으로 
 * 루프를 돈다.
 *
 * matches: IRQCHIP_DECLARE()로 컴파일된 모든 인터럽트 컨트롤러 드라이버
 */
	for_each_matching_node(np, matches) {
		if (!of_find_property(np, "interrupt-controller", NULL) ||
				!of_device_is_available(np))
			continue;
		/*
		 * Here, we allocate and populate an intc_desc with the node
		 * pointer, interrupt-parent device_node etc.
		 */
		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (WARN_ON(!desc))
			goto err;

		desc->dev = np;

/* IAMROOT-12:
 * -------------
 * 상위 인터럽트 컨트롤러를 대입한다.
 */
		desc->interrupt_parent = of_irq_find_parent(np);

/* IAMROOT-12:
 * -------------
 * 자기 자신이 부모로 등록된 경우 상위 인터럽트 컨트롤러가 없는 경우이다.
 */
		if (desc->interrupt_parent == np)
			desc->interrupt_parent = NULL;
		list_add_tail(&desc->list, &intc_desc_list);
	}

	/*
	 * The root irq controller is the one without an interrupt-parent.
	 * That one goes first, followed by the controllers that reference it,
	 * followed by the ones that reference the 2nd level controllers, etc.
	 */
	/* IAMROOT-12 fehead (2017-04-01):
	 * --------------------------
	 * 루트 irq 컨트롤러는 interrupt-parent가없는 컨트롤러입니다. 먼저
	 * 컨트롤러를 참조한 다음 2 번째 레벨 컨트롤러를 참조하는 컨트롤러
	 * 등이옵니다.
	 */
	while (!list_empty(&intc_desc_list)) {
		/*
		 * Process all controllers with the current 'parent'.
		 * First pass will be looking for NULL as the parent.
		 * The assumption is that NULL parent means a root controller.
		 */
		list_for_each_entry_safe(desc, temp_desc, &intc_desc_list, list) {
			const struct of_device_id *match;
			int ret;
			of_irq_init_cb_t irq_init_cb;

/* IAMROOT-12:
 * -------------
 * parent가 처음에는 null이므로 null인 디스크립터들만 처리한다.
 *
 * 두 번째 루프를 돌때에는 루트 노드 중 하나를 순서대로 처리한다.
 */
			if (desc->interrupt_parent != parent)
				continue;

			list_del(&desc->list);
			match = of_match_node(matches, desc->dev);
			if (WARN(!match->data,
			    "of_irq_init: no init function for %s\n",
			    match->compatible)) {
				kfree(desc);
				continue;
			}

			pr_debug("of_irq_init: init %s @ %p, parent %p\n",
				 match->compatible,
				 desc->dev, desc->interrupt_parent);

			/* IAMROOT-12 fehead (2017-04-01):
			 * --------------------------
			 * pi2 
			 *	bcm2836_arm_irqchip_l1_intc_of_init
			 * 	bcm2836_armctrl_of_init
			 * arm gic
			 *	gic_of_init
			 */
			irq_init_cb = (of_irq_init_cb_t)match->data;

/* IAMROOT-12:
 * -------------
 * 인터럽트 컨트롤러 초기화 함수를 호출한다.
 * rpi2: 1) bcm2836_arm_irqchip_l1_intc_of_init()
 *       2) bcm2836_armctrl_of_init()
 * gic:  *) gic_of_init()
 */
			ret = irq_init_cb(desc->dev, desc->interrupt_parent);
			if (ret) {
				kfree(desc);
				continue;
			}

			/*
			 * This one is now set up; add it to the parent list so
			 * its children can get processed in a subsequent pass.
			 */
			list_add_tail(&desc->list, &intc_parent_list);
		}

		/* Get the next pending parent that might have children */
		desc = list_first_entry_or_null(&intc_parent_list,
						typeof(*desc), list);
		if (!desc) {
			pr_err("of_irq_init: children remain, but no parents\n");
			break;
		}
		list_del(&desc->list);
		parent = desc->dev;
		kfree(desc);
	}

	list_for_each_entry_safe(desc, temp_desc, &intc_parent_list, list) {
		list_del(&desc->list);
		kfree(desc);
	}
err:
	list_for_each_entry_safe(desc, temp_desc, &intc_desc_list, list) {
		list_del(&desc->list);
		kfree(desc);
	}
}
