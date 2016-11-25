/*
 *  arch/arm/include/asm/mach/arch.h
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>

#ifndef __ASSEMBLY__
#include <linux/reboot.h>

struct tag;
struct pt_regs;
struct smp_operations;
#ifdef CONFIG_SMP
#define smp_ops(ops) (&(ops))
#define smp_init_ops(ops) (&(ops))
#else
#define smp_ops(ops) (struct smp_operations *)NULL
#define smp_init_ops(ops) (bool (*)(void))NULL
#endif

/* IAMROOT-12A:
 * ------------
 * 머신 디스크립터로 CPU 아키텍처 및 시스템을 구분하여 인식하게된 경우
 * 관련 변수들이 이 구조체에 담긴다.
 *
 * nr: ATAG를 사용하는 방식에서는 부트로더가 r1 레지스터 머신 넘버를 
 *     커널에게 전달해준 번호를 사용하여 머신을 검색.
 * name: DTB를 사용하는 방식에서는 커널이 name 문자열로 머신을 검색한다.
 */

struct machine_desc {
	unsigned int		nr;		/* architecture number	*/
	const char		*name;		/* architecture name	*/
	unsigned long		atag_offset;	/* tagged list (relative) */
	const char *const 	*dt_compat;	/* array of device tree
						 * 'compatible' strings	*/

	unsigned int		nr_irqs;	/* number of IRQs */

#ifdef CONFIG_ZONE_DMA
	phys_addr_t		dma_zone_size;	/* size of DMA-able area */
#endif

	unsigned int		video_start;	/* start of video RAM	*/
	unsigned int		video_end;	/* end of video RAM	*/

	unsigned char		reserve_lp0 :1;	/* never has lp0	*/
	unsigned char		reserve_lp1 :1;	/* never has lp1	*/
	unsigned char		reserve_lp2 :1;	/* never has lp2	*/
	enum reboot_mode	reboot_mode;	/* default restart mode	*/
	unsigned		l2c_aux_val;	/* L2 cache aux value	*/
	unsigned		l2c_aux_mask;	/* L2 cache aux mask	*/
	void			(*l2c_write_sec)(unsigned long, unsigned);
	struct smp_operations	*smp;		/* SMP operations	*/
	bool			(*smp_init)(void);
	void			(*fixup)(struct tag *, char **);
	void			(*dt_fixup)(void);
	void			(*init_meminfo)(void);
	void			(*reserve)(void);/* reserve mem blocks	*/
	void			(*map_io)(void);/* IO mapping function	*/
	void			(*init_early)(void);
	void			(*init_irq)(void);
	void			(*init_time)(void);
	void			(*init_machine)(void);
	void			(*init_late)(void);
#ifdef CONFIG_MULTI_IRQ_HANDLER
	void			(*handle_irq)(struct pt_regs *);
#endif
	void			(*restart)(enum reboot_mode, const char *);
};

/*
 * Current machine - only accessible during boot.
 */
/* IAMROOT-12D (2016-05-26):
 * --------------------------
 * static const struct machine_desc __mach_desc_BCM_2709
 * _used
 *  __attribute__((__section__(".arch.info.init"))) = {
 *		.nr     = MACH_TYPE_BCM_2709
 *		.name   = "BCM_2709",
 *		.smp		= smp_ops(bcm2709_smp_ops),
 *		.map_io = bcm2709_map_io,
 *		.init_irq = bcm2709_init_irq,
 *		.init_time = bcm2709_timer_init,
 *		.init_machine = bcm2709_init,
 *		.init_early = bcm2709_init_early,
 *		.reserve = board_reserve,
 *		.restart	= bcm2709_restart,
 *		.dt_compat = bcm2709_compat,
 * };
 */
extern const struct machine_desc *machine_desc;

/*
 * Machine type table - also only accessible during boot
 */
extern const struct machine_desc __arch_info_begin[], __arch_info_end[];
#define for_each_machine_desc(p)			\
	for (p = __arch_info_begin; p < __arch_info_end; p++)

/*
 * Set of macros to define architecture features.  This is built into
 * a table by the linker.
 */
#define MACHINE_START(_type,_name)			\
static const struct machine_desc __mach_desc_##_type	\
 __used							\
 __attribute__((__section__(".arch.info.init"))) = {	\
	.nr		= MACH_TYPE_##_type,		\
	.name		= _name,

#define MACHINE_END				\
};

#define DT_MACHINE_START(_name, _namestr)		\
static const struct machine_desc __mach_desc_##_name	\
 __used							\
 __attribute__((__section__(".arch.info.init"))) = {	\
	.nr		= ~0,				\
	.name		= _namestr,

#endif
