/*
 *  linux/arch/arm/mm/init.c
 *
 *  Copyright (C) 1995-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mman.h>
#include <linux/export.h>
#include <linux/nodemask.h>
#include <linux/initrd.h>
#include <linux/of_fdt.h>
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/dma-contiguous.h>
#include <linux/sizes.h>

#include <asm/cp15.h>
#include <asm/mach-types.h>
#include <asm/memblock.h>
#include <asm/prom.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/system_info.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "mm.h"


/* IAMROOT-12AB:
 * -------------
 * 기존 SCTLR 저장되어 있는 값에 mask bit들만 제외시키고 
 * 전역 변수 cr_alignment에 저장시킨 후 리턴한다.
 */
#ifdef CONFIG_CPU_CP15_MMU
unsigned long __init __clear_cr(unsigned long mask)
{
	cr_alignment = cr_alignment & ~mask;
	return cr_alignment;
}
#endif

static phys_addr_t phys_initrd_start __initdata = 0;
static unsigned long phys_initrd_size __initdata = 0;

static int __init early_initrd(char *p)
{
	phys_addr_t start;
	unsigned long size;
	char *endp;

	start = memparse(p, &endp);
	if (*endp == ',') {
		size = memparse(endp + 1, NULL);

		phys_initrd_start = start;
		phys_initrd_size = size;
	}
	return 0;
}
early_param("initrd", early_initrd);

static int __init parse_tag_initrd(const struct tag *tag)
{
	pr_warn("ATAG_INITRD is deprecated; "
		"please update your bootloader.\n");
	phys_initrd_start = __virt_to_phys(tag->u.initrd.start);
	phys_initrd_size = tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD, parse_tag_initrd);

static int __init parse_tag_initrd2(const struct tag *tag)
{
	phys_initrd_start = tag->u.initrd.start;
	phys_initrd_size = tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD2, parse_tag_initrd2);

/*
 * This keeps memory configuration data used by a couple memory
 * initialization functions, as well as show_mem() for the skipping
 * of holes in the memory map.  It is populated by arm_add_memory().
 */
void show_mem(unsigned int filter)
{
	int free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0, slab = 0;
	struct memblock_region *reg;

	printk("Mem-info:\n");
	show_free_areas(filter);

	for_each_memblock (memory, reg) {
		unsigned int pfn1, pfn2;
		struct page *page, *end;

		pfn1 = memblock_region_memory_base_pfn(reg);
		pfn2 = memblock_region_memory_end_pfn(reg);

		page = pfn_to_page(pfn1);
		end  = pfn_to_page(pfn2 - 1) + 1;

		do {
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (PageSlab(page))
				slab++;
			else if (!page_count(page))
				free++;
			else
				shared += page_count(page) - 1;
			pfn1++;
			page = pfn_to_page(pfn1);
		} while (pfn1 < pfn2);
	}

	printk("%d pages of RAM\n", total);
	printk("%d free pages\n", free);
	printk("%d reserved pages\n", reserved);
	printk("%d slab pages\n", slab);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
}

static void __init find_limits(unsigned long *min, unsigned long *max_low,
			       unsigned long *max_high)
{

/* IAMROOT-12AB:
 * -------------
 * max_low: lowmem/highmem 경계
 */
	*max_low = PFN_DOWN(memblock_get_current_limit());

/* IAMROOT-12AB:
 * -------------
 * min: DRAM의 시작 주소 (memblock.memory.regions[0].base)
 */
	*min = PFN_UP(memblock_start_of_DRAM());

/* IAMROOT-12AB:
 * -------------
 * max_high: DRM의 끝 주소 (memblock.memory.region[마지막].base + size)
 */
	*max_high = PFN_DOWN(memblock_end_of_DRAM());
}

#ifdef CONFIG_ZONE_DMA

phys_addr_t arm_dma_zone_size __read_mostly;
EXPORT_SYMBOL(arm_dma_zone_size);

/*
 * The DMA mask corresponding to the maximum bus address allocatable
 * using GFP_DMA.  The default here places no restriction on DMA
 * allocations.  This must be the smallest DMA mask in the system,
 * so a successful GFP_DMA allocation will always satisfy this.
 */
phys_addr_t arm_dma_limit;
unsigned long arm_dma_pfn_limit;

static void __init arm_adjust_dma_zone(unsigned long *size, unsigned long *hole,
	unsigned long dma_size)
{
	if (size[0] <= dma_size)
		return;

/* IAMROOT-12AB:
 * -------------
 * sparse 메모리 모델에서
 *	DMA 사이즈만큼 ZONE_NORMAL에 대해 조정을 한다.
 *	ZONE_HIGHMEM에 대해서는 DMA가 영향을 주지 않으므로 조정하지 않는다.
 */
	size[ZONE_NORMAL] = size[0] - dma_size;
	size[ZONE_DMA] = dma_size;
	hole[ZONE_NORMAL] = hole[0];
	hole[ZONE_DMA] = 0;
}
#endif

void __init setup_dma_zone(const struct machine_desc *mdesc)
{
/* IAMROOT-12AB:
 * -------------
 * DMA(Direct Memory Access)
 *     - PC에서는 address에 24비트를 사용한 ISA 버스를 사용하였기 때문에
 *	 16M 제한이 있었다.
 *     - 32비트 ARM에서는 machine 설계마다 다르다.
 *	 - DMA 디바이스 장치가 32bit 주소를 사용하는 경우 ZONE_DMA를 나눌
 *	   필요가 없다.
 *	 - DMA 디바이스 장치가 32bit 미만의 주소를 사용하는 경우 
 *	   ZONE_DMA를 나누어 사용해야 한다.
 *	   (DMA 디바이스 장치가 20비트 만을 사용하는 경우에는 ZONE_DMA를
 *	   1M로 설정하여 사용한다.)
 */
#ifdef CONFIG_ZONE_DMA
	if (mdesc->dma_zone_size) {
		arm_dma_zone_size = mdesc->dma_zone_size;
		arm_dma_limit = PHYS_OFFSET + arm_dma_zone_size - 1;
	} else
		arm_dma_limit = 0xffffffff;
	arm_dma_pfn_limit = arm_dma_limit >> PAGE_SHIFT;
#endif
}

static void __init zone_sizes_init(unsigned long min, unsigned long max_low,
	unsigned long max_high)
{
	unsigned long zone_size[MAX_NR_ZONES], zhole_size[MAX_NR_ZONES];
	struct memblock_region *reg;

	/*
	 * initialise the zones.
	 */
	memset(zone_size, 0, sizeof(zone_size));

	/*
	 * The memory size has already been determined.  If we need
	 * to do anything fancy with the allocation of this memory
	 * to the zones, now is the time to do it.
	 */

/* IAMROOT-12AB:
 * -------------
 * 먼저 ZONE_NORMAL 영역에 대한 페이지 수를 계산한다.(hole을 포함)
 */
	zone_size[0] = max_low - min;
#ifdef CONFIG_HIGHMEM

/* IAMROOT-12AB:
 * -------------
 * ZONE_HIGHMEM 영역에 대한 페이지 수를 계산한다.(hole을 포함)
 */
	zone_size[ZONE_HIGHMEM] = max_high - max_low;
#endif

	/*
	 * Calculate the size of the holes.
	 *  holes = node_size - sum(bank_sizes)
	 */

/* IAMROOT-12AB:
 * -------------
 * 각 ZONE의 hole의 페이지 수를 계산한다.
 */
	memcpy(zhole_size, zone_size, sizeof(zhole_size));
	for_each_memblock(memory, reg) {
		unsigned long start = memblock_region_memory_base_pfn(reg);
		unsigned long end = memblock_region_memory_end_pfn(reg);

		if (start < max_low) {
			unsigned long low_end = min(end, max_low);
			zhole_size[0] -= low_end - start;
		}
#ifdef CONFIG_HIGHMEM
		if (end > max_low) {
			unsigned long high_start = max(start, max_low);
			zhole_size[ZONE_HIGHMEM] -= end - high_start;
		}
#endif
	}

#ifdef CONFIG_ZONE_DMA
	/*
	 * Adjust the sizes according to any special requirements for
	 * this machine type.
	 */

/* IAMROOT-12AB:
 * -------------
 * DMA를 사용하는 경우 ZONE_NORMAL은 DMA_SIZE 만큼 감소시킨다.
 */
	if (arm_dma_zone_size)
		arm_adjust_dma_zone(zone_size, zhole_size,
			arm_dma_zone_size >> PAGE_SHIFT);
#endif


/* IAMROOT-12AB:
 * -------------
 * 노드 0에 대해서만 초기화를 수행한다.
 *	zone_size[]:	zone별로 hole을 포함한 page 갯수
 *	zhole_size[]:	zone별로 hole page 갯수
 *	min:		시작 메모리 pfn (노드별)
 */
	free_area_init_node(0, zone_size, min, zhole_size);
}

#ifdef CONFIG_HAVE_ARCH_PFN_VALID
int pfn_valid(unsigned long pfn)
{
	return memblock_is_memory(__pfn_to_phys(pfn));
}
EXPORT_SYMBOL(pfn_valid);
#endif

#ifndef CONFIG_SPARSEMEM
static void __init arm_memory_present(void)
{
}
#else
static void __init arm_memory_present(void)
{
	struct memblock_region *reg;

/* IAMROOT-12AB:
 * -------------
 * 메모리가 존재하는 섹션(mem_section[])을 초기화한다.
 * 노드 id와 섹션별 메모리 존재 플래그 비트만으로 section_mem_map을 초기화한다.
 * (추후 섹션별 생성되는 mem_map과 연결될 때 노드 id를 삭제한다.)
 */
	for_each_memblock(memory, reg)
		memory_present(0, memblock_region_memory_base_pfn(reg),
			       memblock_region_memory_end_pfn(reg));
}
#endif

static bool arm_memblock_steal_permitted = true;

phys_addr_t __init arm_memblock_steal(phys_addr_t size, phys_addr_t align)
{
	phys_addr_t phys;

	BUG_ON(!arm_memblock_steal_permitted);

	phys = memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ANYWHERE);
	memblock_free(phys, size);
	memblock_remove(phys, size);

	return phys;
}

void __init arm_memblock_init(const struct machine_desc *mdesc)
{
	/* Register the kernel text, kernel data and initrd with memblock. */

/* IAMROOT-12AB:
 * -------------
 * XIP 커널인 경우는 ROM에 커널 코드가 들어있어서 코드 부분은 제외하고 
 * 데이터 부분만 reserve한다.
 */
#ifdef CONFIG_XIP_KERNEL
	memblock_reserve(__pa(_sdata), _end - _sdata);
#else
	memblock_reserve(__pa(_stext), _end - _stext);
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	/* FDT scan will populate initrd_start */
	if (initrd_start && !phys_initrd_size) {
		phys_initrd_start = __virt_to_phys(initrd_start);
		phys_initrd_size = initrd_end - initrd_start;
	}
	initrd_start = initrd_end = 0;

/* IAMROOT-12AB:
 * -------------
 * memory memblock에 전체 initrd 영역이 포함되지 않은 경우 initrd 영역 등록 포기
 */
	if (phys_initrd_size &&
	    !memblock_is_region_memory(phys_initrd_start, phys_initrd_size)) {
		pr_err("INITRD: 0x%08llx+0x%08lx is not a memory region - disabling initrd\n",
		       (u64)phys_initrd_start, phys_initrd_size);
		phys_initrd_start = phys_initrd_size = 0;
	}
/* IAMROOT-12AB:
 * -------------
 * reserved memblock의 일부라도 이미 initrd 영역을 사용하는 경우 initrd 영역 등록 포기
 */
	if (phys_initrd_size &&
	    memblock_is_region_reserved(phys_initrd_start, phys_initrd_size)) {
		pr_err("INITRD: 0x%08llx+0x%08lx overlaps in-use memory region - disabling initrd\n",
		       (u64)phys_initrd_start, phys_initrd_size);
		phys_initrd_start = phys_initrd_size = 0;
	}
	if (phys_initrd_size) {
		memblock_reserve(phys_initrd_start, phys_initrd_size);

		/* Now convert initrd to virtual addresses */
		initrd_start = __phys_to_virt(phys_initrd_start);
		initrd_end = initrd_start + phys_initrd_size;
	}
#endif

/* IAMROOT-12AB:
 * -------------
 * 페이지 디렉토리를 reserve에 등록
 */
	arm_mm_memblock_reserve();

/* IAMROOT-12AB:
 * -------------
 * 머신이 직접 reserve를 하는 case에 호출될 수 있다.
 */
	/* reserve any platform specific memblock areas */
	if (mdesc->reserve)
		mdesc->reserve();

/* IAMROOT-12AB:
 * -------------
 * 1) DTB 영역 자체를 reserve 한다.
 * 2) DTB의 memory reservation block에서 읽어들인 주소와 사이즈로 reserve 한다.
 * 3) reserved-memory 노드에 등록된 값을 reserve 한다.
 */
	early_init_fdt_scan_reserved_mem();

	/* reserve memory for DMA contiguous allocations */

/* IAMROOT-12AB:
 * -------------
 * cma 영역을 reserve하고 cma_areas[] 및 dma_mmu_remap[]에도 정보를 추가한다.
 */
	dma_contiguous_reserve(arm_dma_limit);

	arm_memblock_steal_permitted = false;
	memblock_dump_all();
}

void __init bootmem_init(void)
{
	unsigned long min, max_low, max_high;

/* IAMROOT-12AB:
 * -------------
 * memblock의 최대 관리 수가 확장될 수 있도록 enable한다.
 */
	memblock_allow_resize();
	max_low = max_high = 0;

/* IAMROOT-12AB:
 * -------------
 * DRAM 시작 물리주소, lowmem/highmem 경계 물리주소, DRAM 끝 물리주소
 */
	find_limits(&min, &max_low, &max_high);

	/*
	 * Sparsemem tries to allocate bootmem in memory_present(),
	 * so must be done after the fixed reservations
	 */

/* IAMROOT-12AB:
 * -------------
 * 메모리가 존재하는 섹션(mem_section[])을 초기화한다.
 */
	arm_memory_present();

	/*
	 * sparse_init() needs the bootmem allocator up and running.
	 */

/* IAMROOT-12AB:
 * -------------
 * mem_section[]과 연결된 usemap과 mem_map 공간을 할당받고 연결시킨다.
 */
	sparse_init();

	/*
	 * Now free the memory - free_area_init_node needs
	 * the sparse mem_map arrays initialized by sparse_init()
	 * for memmap_init_zone(), otherwise all PFNs are invalid.
	 */

/* IAMROOT-12AB:
 * -------------
 * - 노드별 zone 구획을 설정하고 present/spanned page 수를 설정한다.
 *   (NUMA 시스템의 경우 ZONE_HIGHMEM 포함)
 * - 노드별 정보 초기화 
 *   .node_mem_map 할당 및 초기화 
 *   .usemap 할당 및 초기화
 */
	zone_sizes_init(min, max_low, max_high);

	/*
	 * This doesn't seem to be used by the Linux memory manager any
	 * more, but is used by ll_rw_block.  If we can get rid of it, we
	 * also get rid of some of the stuff above as well.
	 */
	min_low_pfn = min;
	max_low_pfn = max_low;
	max_pfn = max_high;
}

/*
 * Poison init memory with an undefined instruction (ARM) or a branch to an
 * undefined instruction (Thumb).
 */
static inline void poison_init_mem(void *s, size_t count)
{
	u32 *p = (u32 *)s;
	for (; count != 0; count -= 4)
		*p++ = 0xe7fddef0;
}

static inline void
free_memmap(unsigned long start_pfn, unsigned long end_pfn)
{
	struct page *start_pg, *end_pg;
	phys_addr_t pg, pgend;

	/*
	 * Convert start_pfn/end_pfn to a struct page pointer.
	 */
	start_pg = pfn_to_page(start_pfn - 1) + 1;
	end_pg = pfn_to_page(end_pfn - 1) + 1;

	/*
	 * Convert to physical addresses, and
	 * round start upwards and end downwards.
	 */
	pg = PAGE_ALIGN(__pa(start_pg));
	pgend = __pa(end_pg) & PAGE_MASK;

	/*
	 * If there are free pages between these,
	 * free the section of the memmap array.
	 */
	if (pg < pgend)
		memblock_free_early(pg, pgend - pg);
}

/*
 * The mem_map array can get very big.  Free the unused area of the memory map.
 */

/* IAMROOT-12AB:
 * -------------
 * 실제 메모리를 관리하지 않는 mem_map 공간을 제거(free)한다.
 */
static void __init free_unused_memmap(void)
{
	unsigned long start, prev_end = 0;
	struct memblock_region *reg;

	/*
	 * This relies on each bank being in address order.
	 * The banks are sorted previously in bootmem_init().
	 */
	for_each_memblock(memory, reg) {
		start = memblock_region_memory_base_pfn(reg);

#ifdef CONFIG_SPARSEMEM
		/*
		 * Take care not to free memmap entries that don't exist
		 * due to SPARSEMEM sections which aren't present.
		 */
		start = min(start,
				 ALIGN(prev_end, PAGES_PER_SECTION));
#else
		/*
		 * Align down here since the VM subsystem insists that the
		 * memmap entries are valid from the bank start aligned to
		 * MAX_ORDER_NR_PAGES.
		 */

/* IAMROOT-12AB:
 * -------------
 * mem_map은 최소 MAX_ORDER_NR_PAGES(arm32=4M) 메모리 공간단위로 관리한다.
 */
		start = round_down(start, MAX_ORDER_NR_PAGES);
#endif
		/*
		 * If we had a previous bank, and there is a space
		 * between the current bank and the previous, free it.
		 */
/* IAMROOT-12AB:
 * -------------
 * 이전 memblock의 끝 주소와 현재 memblock의 시작 주소간에 공간이 있는 경우 
 * 그 공간을 커버하는 mem_map을 free 한다.
 */
		if (prev_end && prev_end < start)
			free_memmap(prev_end, start);

		/*
		 * Align up here since the VM subsystem insists that the
		 * memmap entries are valid from the bank end aligned to
		 * MAX_ORDER_NR_PAGES.
		 */
		prev_end = ALIGN(memblock_region_memory_end_pfn(reg),
				 MAX_ORDER_NR_PAGES);
	}

/* IAMROOT-12AB:
 * -------------
 * 남은 메모리가 섹션보다 작은 경우 관리할 필요가 없는 mem_map 공간을 free 한다.
 */
#ifdef CONFIG_SPARSEMEM
	if (!IS_ALIGNED(prev_end, PAGES_PER_SECTION))
		free_memmap(prev_end,
			    ALIGN(prev_end, PAGES_PER_SECTION));
#endif
}

#ifdef CONFIG_HIGHMEM
static inline void free_area_high(unsigned long pfn, unsigned long end)
{
	for (; pfn < end; pfn++)
		free_highmem_page(pfn_to_page(pfn));
}
#endif

static void __init free_highpages(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long max_low = max_low_pfn;
	struct memblock_region *mem, *res;

	/* set highmem page free */
	for_each_memblock(memory, mem) {
		unsigned long start = memblock_region_memory_base_pfn(mem);
		unsigned long end = memblock_region_memory_end_pfn(mem);

		/* Ignore complete lowmem entries */
		if (end <= max_low)
			continue;

		/* Truncate partial highmem entries */
		if (start < max_low)
			start = max_low;

		/* Find and exclude any reserved regions */
		for_each_memblock(reserved, res) {
			unsigned long res_start, res_end;

			res_start = memblock_region_reserved_base_pfn(res);
			res_end = memblock_region_reserved_end_pfn(res);

			if (res_end < start)
				continue;
			if (res_start < start)
				res_start = start;
			if (res_start > end)
				res_start = end;
			if (res_end > end)
				res_end = end;

/* IAMROOT-12:
 * -------------
 * reserved memblock 사이의 free 공간을 버디시스템으로 migration한다.
 */
			if (res_start != start)
				free_area_high(start, res_start);
			start = res_end;
			if (start == end)
				break;
		}

		/* And now free anything which remains */
		if (start < end)
			free_area_high(start, end);
	}
#endif
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
void __init mem_init(void)
{
#ifdef CONFIG_HAVE_TCM
	/* These pointers are filled in on TCM detection */
	extern u32 dtcm_end;
	extern u32 itcm_end;
#endif

/* IAMROOT-12AB:
 * -------------
 * 전역 max_mapnr에 유효한 mem_map 최대 갯수
 */
	set_max_mapnr(pfn_to_page(max_pfn) - mem_map);

	/* this will put all unused low memory onto the freelists */
	free_unused_memmap();
	free_all_bootmem();

#ifdef CONFIG_SA1111
	/* now that our DMA memory is actually so designated, we can free it */
	free_reserved_area(__va(PHYS_OFFSET), swapper_pg_dir, -1, NULL);
#endif

	free_highpages();

	mem_init_print_info(NULL);

#define MLK(b, t) b, t, ((t) - (b)) >> 10
#define MLM(b, t) b, t, ((t) - (b)) >> 20
#define MLK_ROUNDUP(b, t) b, t, DIV_ROUND_UP(((t) - (b)), SZ_1K)

	pr_notice("Virtual kernel memory layout:\n"
			"    vector  : 0x%08lx - 0x%08lx   (%4ld kB)\n"
#ifdef CONFIG_HAVE_TCM
			"    DTCM    : 0x%08lx - 0x%08lx   (%4ld kB)\n"
			"    ITCM    : 0x%08lx - 0x%08lx   (%4ld kB)\n"
#endif
			"    fixmap  : 0x%08lx - 0x%08lx   (%4ld kB)\n"
			"    vmalloc : 0x%08lx - 0x%08lx   (%4ld MB)\n"
			"    lowmem  : 0x%08lx - 0x%08lx   (%4ld MB)\n"
#ifdef CONFIG_HIGHMEM
			"    pkmap   : 0x%08lx - 0x%08lx   (%4ld MB)\n"
#endif
#ifdef CONFIG_MODULES
			"    modules : 0x%08lx - 0x%08lx   (%4ld MB)\n"
#endif
			"      .text : 0x%p" " - 0x%p" "   (%4td kB)\n"
			"      .init : 0x%p" " - 0x%p" "   (%4td kB)\n"
			"      .data : 0x%p" " - 0x%p" "   (%4td kB)\n"
			"       .bss : 0x%p" " - 0x%p" "   (%4td kB)\n",

			MLK(UL(CONFIG_VECTORS_BASE), UL(CONFIG_VECTORS_BASE) +
				(PAGE_SIZE)),
#ifdef CONFIG_HAVE_TCM
			MLK(DTCM_OFFSET, (unsigned long) dtcm_end),
			MLK(ITCM_OFFSET, (unsigned long) itcm_end),
#endif
			MLK(FIXADDR_START, FIXADDR_END),
			MLM(VMALLOC_START, VMALLOC_END),
			MLM(PAGE_OFFSET, (unsigned long)high_memory),
#ifdef CONFIG_HIGHMEM
			MLM(PKMAP_BASE, (PKMAP_BASE) + (LAST_PKMAP) *
				(PAGE_SIZE)),
#endif
#ifdef CONFIG_MODULES
			MLM(MODULES_VADDR, MODULES_END),
#endif

			MLK_ROUNDUP(_text, _etext),
			MLK_ROUNDUP(__init_begin, __init_end),
			MLK_ROUNDUP(_sdata, _edata),
			MLK_ROUNDUP(__bss_start, __bss_stop));

#undef MLK
#undef MLM
#undef MLK_ROUNDUP

	/*
	 * Check boundaries twice: Some fundamental inconsistencies can
	 * be detected at build time already.
	 */
#ifdef CONFIG_MMU
	BUILD_BUG_ON(TASK_SIZE				> MODULES_VADDR);
	BUG_ON(TASK_SIZE 				> MODULES_VADDR);
#endif

#ifdef CONFIG_HIGHMEM
	BUILD_BUG_ON(PKMAP_BASE + LAST_PKMAP * PAGE_SIZE > PAGE_OFFSET);
	BUG_ON(PKMAP_BASE + LAST_PKMAP * PAGE_SIZE	> PAGE_OFFSET);
#endif

	if (PAGE_SIZE >= 16384 && get_num_physpages() <= 128) {
		extern int sysctl_overcommit_memory;
		/*
		 * On a machine this small we won't get
		 * anywhere without overcommit, so turn
		 * it on by default.
		 */
		sysctl_overcommit_memory = OVERCOMMIT_ALWAYS;
	}
}

#ifdef CONFIG_ARM_KERNMEM_PERMS
struct section_perm {
	unsigned long start;
	unsigned long end;
	pmdval_t mask;
	pmdval_t prot;
	pmdval_t clear;
};

static struct section_perm nx_perms[] = {
	/* Make pages tables, etc before _stext RW (set NX). */
	{
		.start	= PAGE_OFFSET,
		.end	= (unsigned long)_stext,
		.mask	= ~PMD_SECT_XN,
		.prot	= PMD_SECT_XN,
	},
	/* Make init RW (set NX). */
	{
		.start	= (unsigned long)__init_begin,
		.end	= (unsigned long)_sdata,
		.mask	= ~PMD_SECT_XN,
		.prot	= PMD_SECT_XN,
	},
#ifdef CONFIG_DEBUG_RODATA
	/* Make rodata NX (set RO in ro_perms below). */
	{
		.start  = (unsigned long)__start_rodata,
		.end    = (unsigned long)__init_begin,
		.mask   = ~PMD_SECT_XN,
		.prot   = PMD_SECT_XN,
	},
#endif
};

#ifdef CONFIG_DEBUG_RODATA
static struct section_perm ro_perms[] = {
	/* Make kernel code and rodata RX (set RO). */
	{
		.start  = (unsigned long)_stext,
		.end    = (unsigned long)__init_begin,
#ifdef CONFIG_ARM_LPAE
		.mask   = ~L_PMD_SECT_RDONLY,
		.prot   = L_PMD_SECT_RDONLY,
#else
		.mask   = ~(PMD_SECT_APX | PMD_SECT_AP_WRITE),
		.prot   = PMD_SECT_APX | PMD_SECT_AP_WRITE,
		.clear  = PMD_SECT_AP_WRITE,
#endif
	},
};
#endif

/*
 * Updates section permissions only for the current mm (sections are
 * copied into each mm). During startup, this is the init_mm. Is only
 * safe to be called with preemption disabled, as under stop_machine().
 */
static inline void section_update(unsigned long addr, pmdval_t mask,
				  pmdval_t prot)
{
	struct mm_struct *mm;
	pmd_t *pmd;

	mm = current->active_mm;
	pmd = pmd_offset(pud_offset(pgd_offset(mm, addr), addr), addr);

#ifdef CONFIG_ARM_LPAE
	pmd[0] = __pmd((pmd_val(pmd[0]) & mask) | prot);
#else
	if (addr & SECTION_SIZE)
		pmd[1] = __pmd((pmd_val(pmd[1]) & mask) | prot);
	else
		pmd[0] = __pmd((pmd_val(pmd[0]) & mask) | prot);
#endif
	flush_pmd_entry(pmd);
	local_flush_tlb_kernel_range(addr, addr + SECTION_SIZE);
}

/* Make sure extended page tables are in use. */
static inline bool arch_has_strict_perms(void)
{
	if (cpu_architecture() < CPU_ARCH_ARMv6)
		return false;

	return !!(get_cr() & CR_XP);
}

#define set_section_perms(perms, field)	{				\
	size_t i;							\
	unsigned long addr;						\
									\
	if (!arch_has_strict_perms())					\
		return;							\
									\
	for (i = 0; i < ARRAY_SIZE(perms); i++) {			\
		if (!IS_ALIGNED(perms[i].start, SECTION_SIZE) ||	\
		    !IS_ALIGNED(perms[i].end, SECTION_SIZE)) {		\
			pr_err("BUG: section %lx-%lx not aligned to %lx\n", \
				perms[i].start, perms[i].end,		\
				SECTION_SIZE);				\
			continue;					\
		}							\
									\
		for (addr = perms[i].start;				\
		     addr < perms[i].end;				\
		     addr += SECTION_SIZE)				\
			section_update(addr, perms[i].mask,		\
				       perms[i].field);			\
	}								\
}

static inline void fix_kernmem_perms(void)
{
	set_section_perms(nx_perms, prot);
}

#ifdef CONFIG_DEBUG_RODATA
void mark_rodata_ro(void)
{
	set_section_perms(ro_perms, prot);
}

void set_kernel_text_rw(void)
{
	set_section_perms(ro_perms, clear);
}

void set_kernel_text_ro(void)
{
	set_section_perms(ro_perms, prot);
}
#endif /* CONFIG_DEBUG_RODATA */

#else
static inline void fix_kernmem_perms(void) { }
#endif /* CONFIG_ARM_KERNMEM_PERMS */

void free_tcmmem(void)
{
#ifdef CONFIG_HAVE_TCM
	extern char __tcm_start, __tcm_end;

	poison_init_mem(&__tcm_start, &__tcm_end - &__tcm_start);
	free_reserved_area(&__tcm_start, &__tcm_end, -1, "TCM link");
#endif
}

void free_initmem(void)
{
	fix_kernmem_perms();
	free_tcmmem();

	poison_init_mem(__init_begin, __init_end - __init_begin);
	if (!machine_is_integrator() && !machine_is_cintegrator())
		free_initmem_default(-1);
}

#ifdef CONFIG_BLK_DEV_INITRD

static int keep_initrd;

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (!keep_initrd) {
		if (start == initrd_start)
			start = round_down(start, PAGE_SIZE);
		if (end == initrd_end)
			end = round_up(end, PAGE_SIZE);

		poison_init_mem((void *)start, PAGE_ALIGN(end) - start);
		free_reserved_area((void *)start, (void *)end, -1, "initrd");
	}
}

static int __init keepinitrd_setup(char *__unused)
{
	keep_initrd = 1;
	return 1;
}

__setup("keepinitrd", keepinitrd_setup);
#endif
