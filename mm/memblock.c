/*
 * Procedures for maintaining information about logical memory blocks.
 *
 * Peter Bergner, IBM Corp.	June 2001.
 * Copyright (C) 2001 Peter Bergner.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/poison.h>
#include <linux/pfn.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/memblock.h>

#include <asm-generic/sections.h>
#include <linux/io.h>

#include "internal.h"


/* IAMROOT-12AB:
 * -------------
 * 아래 영역은 각 memblock region에 초기 커널이 생성될 때 만들어진다.(128/128/4)
 * 추후 이 영역이 부족해지면 이 영역을 버리고 새로운 페이지를 할당 받아 
 * 사용할 수 있다.
 */
static struct memblock_region memblock_memory_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;
static struct memblock_region memblock_reserved_init_regions[INIT_MEMBLOCK_REGIONS] __initdata_memblock;
#ifdef CONFIG_HAVE_MEMBLOCK_PHYS_MAP
static struct memblock_region memblock_physmem_init_regions[INIT_PHYSMEM_REGIONS] __initdata_memblock;
#endif


/* IAMROOT-12AB:
 * -------------
 * __initdata_memblock: CONFIG_ARCH_DISCARD_MEMBLOCK 커널 옵션에 따라 .data 섹션 
 * 또는 .meminit.data 섹션에 위치하게 된다.
 *
 * bottom_up: true=메모리 할당 요청시 아래에서 위로 영역을 검색
 * current_limit: 메모리 영역 제한 시 설정된다.
 *                초기 값 MEMBLOCK_ALLOC_ANYWHERE의 경우 물리 주소 
 *				한계 값(0xffff_ffff)을 가진다.
 *                        MEMBLOCK_ALLOC_ACCESSIBLE의 경우 current_limit까지 
 *
 * cnt <- 시작 부터 1로 설정(empty)
 */
/* IAMROOT-12CD (2016-07-23):
 * --------------------------
 * .momory.total_size = 0x3c00 0000 초기값. 약 960M
 */
/* IAMROOT-12CD (2016-08-16):
 * --------------------------
 * memblock.reserved {
 *	cnt = 3, max = 128, total_size = 9795242,
 *	regions[0] = {base = 0x4000(page table), size = 0x4000, flags = 0x0},
 *	regions[1] = {base = 0x8240(_stext), size = 9737564, flags = 0},
 *	regions[2] = {base = 0x8000000(fdt), size = 41294, flags = 0},
 *	regions[3] = {base = 0, size = 0, flags = 0},
 *	...
 */
struct memblock memblock __initdata_memblock = {
/* IAMROOT-12CD (2016-08-20):
 * --------------------------
 * .memory
 * {cnt = 0x1, max = 0x80, total_size = 0x3c000000, regions = {
 *   [0] = {base = 0x0, size = 0x3c000000, flags = 0x0}, 0 ~ 960M 영역.
 *   [1] = {base = 0x0, size = 0x0, flags = 0x0},
 *   ...
 * }}
 */
	.memory.regions		= memblock_memory_init_regions,
	.memory.cnt		= 1,	/* empty dummy entry */
	.memory.max		= INIT_MEMBLOCK_REGIONS,

/* IAMROOT-12CD (2016-08-20):
 * --------------------------
 * .reserved
 * {cnt = 0x4, max = 0x80, total_size = 18183850, regions = {
 *  [0] = {base = 0x4000, size = 0x4000, flags = 0x0},	page table
 *  [1] = {base = 0x8240, size = 9737564, flags = 0x0},	커널 영역
 *  [2] = {base = 0x8000000, size = 41294, flags = 0x0}, fdt 영역
 *  [3] = {base = 0x3b800000, size = 0x800000, flags = 0x0}, cma(dma) 952M~960M
 *  [4] = {base = 0x0, size = 0x0, flags = 0x0},
 *  ...
 * } }
 */
	.reserved.regions	= memblock_reserved_init_regions,
	.reserved.cnt		= 1,	/* empty dummy entry */
	.reserved.max		= INIT_MEMBLOCK_REGIONS,

#ifdef CONFIG_HAVE_MEMBLOCK_PHYS_MAP
	.physmem.regions	= memblock_physmem_init_regions,
	.physmem.cnt		= 1,	/* empty dummy entry */
	.physmem.max		= INIT_PHYSMEM_REGIONS,
#endif

	.bottom_up		= false,
	/* IAMROOT-12CD (2016-08-06):
	 * current_limit = 0x3c000000(960mb)
	 */
	.current_limit		= MEMBLOCK_ALLOC_ANYWHERE,
};

int memblock_debug __initdata_memblock;
#ifdef CONFIG_MOVABLE_NODE
bool movable_node_enabled __initdata_memblock = false;
#endif
/* IAMROOT-12 fehead (2017-01-02):
 * --------------------------
 * memblock_allow_resize() 함수에서 호출
 * memblock_can_resize = 1
 */
static int memblock_can_resize __initdata_memblock;
static int memblock_memory_in_slab __initdata_memblock = 0;
static int memblock_reserved_in_slab __initdata_memblock = 0;

/* inline so we don't get a warning when pr_debug is compiled out */
static __init_memblock const char *
memblock_type_name(struct memblock_type *type)
{
	if (type == &memblock.memory)
		return "memory";
	else if (type == &memblock.reserved)
		return "reserved";
	else
		return "unknown";
}

/* adjust *@size so that (@base + *@size) doesn't overflow, return new size */

/* IAMROOT-12AB:
 * -------------
 * 물리 주소 끝을 초과하는 사이즈를 제한하기 위한 함수
 * - base=0x8000_0000 (2G), size=0xc000_0000 (3G)
 *   -> 0x7fff_ffff
 */
static inline phys_addr_t memblock_cap_size(phys_addr_t base, phys_addr_t *size)
{
	return *size = min(*size, (phys_addr_t)ULLONG_MAX - base);
}

/*
 * Address comparison utilities
 */
static unsigned long __init_memblock memblock_addrs_overlap(phys_addr_t base1, phys_addr_t size1,
				       phys_addr_t base2, phys_addr_t size2)
{

/* IAMROOT-12AB:
 * -------------
 * 영역이 일부라도 겹쳐 있는 경우 true
 */
	return ((base1 < (base2 + size2)) && (base2 < (base1 + size1)));
}


/* IAMROOT-12AB:
 * -------------
 * 겹쳐있는 memblock의 인덱스를 리턴한다. 겹쳐있지 않은 경우 -1이 리턴된다.
 */
static long __init_memblock memblock_overlaps_region(struct memblock_type *type,
					phys_addr_t base, phys_addr_t size)
{
	unsigned long i;

	for (i = 0; i < type->cnt; i++) {
		phys_addr_t rgnbase = type->regions[i].base;
		phys_addr_t rgnsize = type->regions[i].size;
		if (memblock_addrs_overlap(base, size, rgnbase, rgnsize))
			break;
	}

	return (i < type->cnt) ? i : -1;
}

/*
 * __memblock_find_range_bottom_up - find free area utility in bottom-up
 * @start: start of candidate range
 * @end: end of candidate range, can be %MEMBLOCK_ALLOC_{ANYWHERE|ACCESSIBLE}
 * @size: size of free area to find
 * @align: alignment of free area to find
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * Utility called from memblock_find_in_range_node(), find free area bottom-up.
 *
 * RETURNS:
 * Found address on success, 0 on failure.
 */
static phys_addr_t __init_memblock
__memblock_find_range_bottom_up(phys_addr_t start, phys_addr_t end,
				phys_addr_t size, phys_addr_t align, int nid)
{
	phys_addr_t this_start, this_end, cand;
	u64 i;

	for_each_free_mem_range(i, nid, &this_start, &this_end, NULL) {

/* IAMROOT-12AB:
 * -------------
 * clamp()
 *	start 보다 작은 경우 start 값을 리턴하고, 
 *	end 보다 큰 경우 end 값을 리턴
 */
		this_start = clamp(this_start, start, end);
		this_end = clamp(this_end, start, end);

		cand = round_up(this_start, align);
		if (cand < this_end && this_end - cand >= size)
			return cand;
	}

	return 0;
}

/**
 * __memblock_find_range_top_down - find free area utility, in top-down
 * @start: start of candidate range
 * @end: end of candidate range, can be %MEMBLOCK_ALLOC_{ANYWHERE|ACCESSIBLE}
 * @size: size of free area to find
 * @align: alignment of free area to find
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * Utility called from memblock_find_in_range_node(), find free area top-down.
 *
 * RETURNS:
 * Found address on success, 0 on failure.
 */
static phys_addr_t __init_memblock
__memblock_find_range_top_down(phys_addr_t start, phys_addr_t end,
			       phys_addr_t size, phys_addr_t align, int nid)
{
	phys_addr_t this_start, this_end, cand;
	u64 i;


/* IAMROOT-12AB:
 * -------------
 * free 공간(this_start, this_end)을 top-down 검색하여 찾아온다.
 */
	for_each_free_mem_range_reverse(i, nid, &this_start, &this_end, NULL) {

/* IAMROOT-12AB:
 * -------------
 * clamp()
 *	start 보다 작은 경우 start 값을 리턴하고, 
 *	end 보다 큰 경우 end 값을 리턴
 */
		this_start = clamp(this_start, start, end);
		this_end = clamp(this_end, start, end);


/* IAMROOT-12AB:
 * -------------
 * 알아온 free 영역의 끝 주소가 사이즈보다 작으면 next
 * (아래 조건에서 this_end - size)를 하면 마이너스 값이 나오면서
 * 오류(원하지 않는 round_down 값)가 나올 수 있으므로 이를 방지)
 */
		if (this_end < size)
			continue;


/* IAMROOT-12AB:
 * -------------
 * 알아온 free 영역의 범위에 size가 포함될 수 있으면 return cand
 */
		cand = round_down(this_end - size, align);
		if (cand >= this_start)
			return cand;
	}

	return 0;
}

/**
 * memblock_find_in_range_node - find free area in given range and node
 * @size: size of free area to find
 * @align: alignment of free area to find
 * @start: start of candidate range
 * @end: end of candidate range, can be %MEMBLOCK_ALLOC_{ANYWHERE|ACCESSIBLE}
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * Find @size free area aligned to @align in the specified range and node.
 *
 * When allocation direction is bottom-up, the @start should be greater
 * than the end of the kernel image. Otherwise, it will be trimmed. The
 * reason is that we want the bottom-up allocation just near the kernel
 * image so it is highly likely that the allocated memory and the kernel
 * will reside in the same node.
 *
 * If bottom-up allocation failed, will try to allocate memory top-down.
 *
 * RETURNS:
 * Found address on success, 0 on failure.
 */

/* IAMROOT-12AB:
 * -------------
 * start~end 범위에서 size 크기로 검색
 * start는 항상 4K부터 가능(0부터 시작하라고 지시해도 첫 번째 페이지는 회피)
 */
phys_addr_t __init_memblock memblock_find_in_range_node(phys_addr_t size,
					phys_addr_t align, phys_addr_t start,
					phys_addr_t end, int nid)
{
	phys_addr_t kernel_end, ret;

	/* pump up @end */
	if (end == MEMBLOCK_ALLOC_ACCESSIBLE)
		end = memblock.current_limit;

	/* avoid allocating the first page */

/* IAMROOT-12AB:
 * -------------
 * 첫 페이지를 피하고 검색을 하는 이유는 첫 페이지의 시작 주소가 0이고
 * 그 찾은 주소를 리턴하게 되면 호출 루틴에서 0 값을 실패로 규정하였기 
 * 때문이다.
 */
	start = max_t(phys_addr_t, start, PAGE_SIZE);
	end = max(start, end);

/* IAMROOT-12AB:
 * -------------
 * kernel_end: 커널의 끝 물리 주소
 */
	kernel_end = __pa_symbol(_end);

	/*
	 * try bottom-up allocation only when bottom-up mode
	 * is set and @end is above the kernel image.
	 */

/* IAMROOT-12AB:
 * -------------
 * hotswap memory가 지원되는 아키텍처에서 상위 공간부터 할당 받는 경우 
 * memory를 분리할 때 migration이 매우 많이 발생하기 때문에 이러한
 * 아키텍처에서는 bottom-up 할당을 사용한다. 
 *
 * rpi2: 32bit ARM은 아직 hotswap memory 지원이되지 않아 top-down을 사용하여 검색
 */
	if (memblock_bottom_up() && end > kernel_end) {
		phys_addr_t bottom_up_start;

		/* make sure we will allocate above the kernel */
		bottom_up_start = max(start, kernel_end);

		/* ok, try bottom-up allocation first */
		ret = __memblock_find_range_bottom_up(bottom_up_start, end,
						      size, align, nid);
		if (ret)
			return ret;

		/*
		 * we always limit bottom-up allocation above the kernel,
		 * but top-down allocation doesn't have the limit, so
		 * retrying top-down allocation may succeed when bottom-up
		 * allocation failed.
		 *
		 * bottom-up allocation is expected to be fail very rarely,
		 * so we use WARN_ONCE() here to see the stack trace if
		 * fail happens.
		 */
		WARN_ONCE(1, "memblock: bottom-up allocation failed, "
			     "memory hotunplug may be affected\n");
	}

/* IAMROOT-12AB:
 * -------------
 * rpi2: top-down으로 검색
 */
	return __memblock_find_range_top_down(start, end, size, align, nid);
}

/**
 * memblock_find_in_range - find free area in given range
 * @start: start of candidate range
 * @end: end of candidate range, can be %MEMBLOCK_ALLOC_{ANYWHERE|ACCESSIBLE}
 * @size: size of free area to find
 * @align: alignment of free area to find
 *
 * Find @size free area aligned to @align in the specified range.
 *
 * RETURNS:
 * Found address on success, 0 on failure.
 */
phys_addr_t __init_memblock memblock_find_in_range(phys_addr_t start,
					phys_addr_t end, phys_addr_t size,
					phys_addr_t align)
{
	return memblock_find_in_range_node(size, align, start, end,
					    NUMA_NO_NODE);
}

static void __init_memblock memblock_remove_region(struct memblock_type *type, unsigned long r)
{
	type->total_size -= type->regions[r].size;
	memmove(&type->regions[r], &type->regions[r + 1],
		(type->cnt - (r + 1)) * sizeof(type->regions[r]));
	type->cnt--;

/* IAMROOT-12AB:
 * -------------
 * 예외적으로 memblock이 하나도 없는 경우 카운트 값을 1로한다.
 */
	/* Special case for empty arrays */
	if (type->cnt == 0) {
		WARN_ON(type->total_size != 0);
		type->cnt = 1;
		type->regions[0].base = 0;
		type->regions[0].size = 0;
		type->regions[0].flags = 0;
		memblock_set_region_node(&type->regions[0], MAX_NUMNODES);
	}
}

#ifdef CONFIG_ARCH_DISCARD_MEMBLOCK

/* IAMROOT-12AB:
 * -------------
 * memblock.reserved.regions 구조체의 주소를 출력인수 addr에 저장하고 
 * 할당된 엔트리 갯 수를 반환한다.
 */
phys_addr_t __init_memblock get_allocated_memblock_reserved_regions_info(
					phys_addr_t *addr)
{
	if (memblock.reserved.regions == memblock_reserved_init_regions)
		return 0;

	*addr = __pa(memblock.reserved.regions);

	return PAGE_ALIGN(sizeof(struct memblock_region) *
			  memblock.reserved.max);
}

phys_addr_t __init_memblock get_allocated_memblock_memory_regions_info(
					phys_addr_t *addr)
{
	if (memblock.memory.regions == memblock_memory_init_regions)
		return 0;

	*addr = __pa(memblock.memory.regions);

	return PAGE_ALIGN(sizeof(struct memblock_region) *
			  memblock.memory.max);
}

#endif

/**
 * memblock_double_array - double the size of the memblock regions array
 * @type: memblock type of the regions array being doubled
 * @new_area_start: starting address of memory range to avoid overlap with
 * @new_area_size: size of memory range to avoid overlap with
 *
 * Double the size of the @type regions array. If memblock is being used to
 * allocate memory for a new reserved regions array and there is a previously
 * allocated memory range [@new_area_start,@new_area_start+@new_area_size]
 * waiting to be reserved, ensure the memory used by the new array does
 * not overlap.
 *
 * RETURNS:
 * 0 on success, -1 on failure.
 */
static int __init_memblock memblock_double_array(struct memblock_type *type,
						phys_addr_t new_area_start,
						phys_addr_t new_area_size)
{
	struct memblock_region *new_array, *old_array;
	phys_addr_t old_alloc_size, new_alloc_size;
	phys_addr_t old_size, new_size, addr;
	int use_slab = slab_is_available();
	int *in_slab;

	/* We don't allow resizing until we know about the reserved regions
	 * of memory that aren't suitable for allocation
	 */

/* IAMROOT-12AB:
 * -------------
 * bootmem_init() -> memblock_allow_resize() 함수에서 호출되어 리사이즈 기능이
 * enable 된다.
 */
	if (!memblock_can_resize)
		return -1;

	/* Calculate new doubled size */
	old_size = type->max * sizeof(struct memblock_region);
	new_size = old_size << 1;
	/*
	 * We need to allocated new one align to PAGE_SIZE,
	 *   so we can free them completely later.
	 */

/* IAMROOT-12AB:
 * -------------
 * PAGE_ALIGN: 4K round up 
 * 예) PAGE_ALIGN(0x1010)=0x2000
 */
	old_alloc_size = PAGE_ALIGN(old_size);
	new_alloc_size = PAGE_ALIGN(new_size);

	/* Retrieve the slab flag */
	if (type == &memblock.memory)
		in_slab = &memblock_memory_in_slab;
	else
		in_slab = &memblock_reserved_in_slab;

	/* Try to find some space for it.
	 *
	 * WARNING: We assume that either slab_is_available() and we use it or
	 * we use MEMBLOCK for allocations. That means that this is unsafe to
	 * use when bootmem is currently active (unless bootmem itself is
	 * implemented on top of MEMBLOCK which isn't the case yet)
	 *
	 * This should however not be an issue for now, as we currently only
	 * call into MEMBLOCK while it's still active, or much later when slab
	 * is active for memory hotplug operations
	 */
	if (use_slab) {
		new_array = kmalloc(new_size, GFP_KERNEL);
		addr = new_array ? __pa(new_array) : 0;
	} else {
		/* only exclude range when trying to double reserved.regions */

/* IAMROOT-12AB:
 * -------------
 * 확장할 공간(처음 128개 였던 region[])을 요청 range 밖에서 할당을 하기 위해
 * 그 공간을 피해서 위에서 한 번 검색해서 할당하고 실패하는 경우 아래에서
 * 한 번 더 시도를 한다. 
 * 그러나 추가할 memblock 타입이 reserved가 아닌 경우 전체 memory range에서 
 * 수행한다.
 */
		if (type != &memblock.reserved)
			new_area_start = new_area_size = 0;

		addr = memblock_find_in_range(new_area_start + new_area_size,
						memblock.current_limit,
						new_alloc_size, PAGE_SIZE);
		if (!addr && new_area_size)
			addr = memblock_find_in_range(0,
				min(new_area_start, memblock.current_limit),
				new_alloc_size, PAGE_SIZE);

		new_array = addr ? __va(addr) : NULL;
	}
	if (!addr) {
		pr_err("memblock: Failed to double %s array from %ld to %ld entries !\n",
		       memblock_type_name(type), type->max, type->max * 2);
		return -1;
	}

	memblock_dbg("memblock: %s is doubled to %ld at [%#010llx-%#010llx]",
			memblock_type_name(type), type->max * 2, (u64)addr,
			(u64)addr + new_size - 1);

	/*
	 * Found space, we now need to move the array over before we add the
	 * reserved region since it may be our reserved array itself that is
	 * full.
	 */
	memcpy(new_array, type->regions, old_size);
	memset(new_array + type->max, 0, old_size);
	old_array = type->regions;
	type->regions = new_array;
	type->max <<= 1;

	/* Free old array. We needn't free it if the array is the static one */
	if (*in_slab)
		kfree(old_array);

/* IAMROOT-12AB:
 * -------------
 * 처음 사용한 128개의 region[]인 경우는 memblock_free를 할 수 없다.
 * 그러나 그 이후에 추가한 공간들은 memblock_free를 할 수 있다.
 */
	else if (old_array != memblock_memory_init_regions &&
		 old_array != memblock_reserved_init_regions)
		memblock_free(__pa(old_array), old_alloc_size);

	/*
	 * Reserve the new array if that comes from the memblock.  Otherwise, we
	 * needn't do it
	 */
	if (!use_slab)
		BUG_ON(memblock_reserve(addr, new_alloc_size));

	/* Update slab flag */
	*in_slab = use_slab;

	return 0;
}

/**
 * memblock_merge_regions - merge neighboring compatible regions
 * @type: memblock type to scan
 *
 * Scan @type and merge neighboring compatible regions.
 */
static void __init_memblock memblock_merge_regions(struct memblock_type *type)
{
	int i = 0;

/* IAMROOT-12AB:
 * -------------
 * memblock 들이 정확히 인접해 있는 경우만 merge 하여 하나로 만든다.
 * (물론 여러개가 인접해 있으면 그 모두를 하나로 만든다)
 */
	/* cnt never goes below 1 */
	while (i < type->cnt - 1) {
		struct memblock_region *this = &type->regions[i];
		struct memblock_region *next = &type->regions[i + 1];

		if (this->base + this->size != next->base ||
		    memblock_get_region_node(this) !=
		    memblock_get_region_node(next) ||
		    this->flags != next->flags) {
			BUG_ON(this->base + this->size > next->base);
			i++;
			continue;
		}

/* IAMROOT-12AB:
 * -------------
 * 경계가 겹친 경우 아래와 같이 memblock_region을 합친다.
 */
		this->size += next->size;
		/* move forward from next + 1, index of which is i + 2 */
		memmove(next, next + 1, (type->cnt - (i + 2)) * sizeof(*next));
		type->cnt--;
	}
}

/**
 * memblock_insert_region - insert new memblock region
 * @type:	memblock type to insert into
 * @idx:	index for the insertion point
 * @base:	base address of the new region
 * @size:	size of the new region
 * @nid:	node id of the new region
 * @flags:	flags of the new region
 *
 * Insert new memblock region [@base,@base+@size) into @type at @idx.
 * @type must already have extra room to accomodate the new region.
 */
static void __init_memblock memblock_insert_region(struct memblock_type *type,
						   int idx, phys_addr_t base,
						   phys_addr_t size,
						   int nid, unsigned long flags)
{
	struct memblock_region *rgn = &type->regions[idx];

	BUG_ON(type->cnt >= type->max);
	memmove(rgn + 1, rgn, (type->cnt - idx) * sizeof(*rgn));
	rgn->base = base;
	rgn->size = size;
	rgn->flags = flags;
	memblock_set_region_node(rgn, nid);
	type->cnt++;
	type->total_size += size;
}

/**
 * memblock_add_range - add new memblock region
 * @type: memblock type to add new region into
 * @base: base address of the new region
 * @size: size of the new region
 * @nid: nid of the new region
 * @flags: flags of the new region
 *
 * Add new memblock region [@base,@base+@size) into @type.  The new region
 * is allowed to overlap with existing ones - overlaps don't affect already
 * existing regions.  @type is guaranteed to be minimal (all neighbouring
 * compatible regions are merged) after the addition.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init_memblock memblock_add_range(struct memblock_type *type,
				phys_addr_t base, phys_addr_t size,
				int nid, unsigned long flags)
{
	bool insert = false;
	phys_addr_t obase = base;

/* IAMROOT-12AB:
 * -------------
 * memblock_cap_size(): 사이즈가 물리 주소의 끝을 초과하지 않게 조정
 * 예) base=0x8000_0000, size=0xc000_0000 : end=0xffff_ffff 
 */
	phys_addr_t end = base + memblock_cap_size(base, &size);
	int i, nr_new;

	if (!size)
		return 0;

	/* special case for empty array */

/* IAMROOT-12AB:
 * -------------
 * 첫 등록할 때에 아래 조건을 수행
 */
	if (type->regions[0].size == 0) {
		WARN_ON(type->cnt != 1 || type->total_size);
		type->regions[0].base = base;
		type->regions[0].size = size;
		type->regions[0].flags = flags;
		memblock_set_region_node(&type->regions[0], nid);
		type->total_size = size;
		return 0;
	}
repeat:
	/*
	 * The following is executed twice.  Once with %false @insert and
	 * then with %true.  The first counts the number of regions needed
	 * to accomodate the new area.  The second actually inserts them.
	 */
	base = obase;
	nr_new = 0;

	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;


/* IAMROOT-12AB:
 * -------------
 * a) 기존 영역의 시작 주소보다 요청 영역의 끝 주소가 같거나 작은 경우
 */
		if (rbase >= end)
			break;

/* IAMROOT-12AB:
 * -------------
 * b) 기존 영역의 끝 주소보다 요청 영역의 시작 주소가 같거나 큰 경우
 */
		if (rend <= base)
			continue;
		/*
		 * @rgn overlaps.  If it separates the lower part of new
		 * area, insert that portion.
		 */

/* IAMROOT-12AB:
 * -------------
 * c) 기존 영역의 시작 주소가 요청 영역의 시작 주소보다 큰 경우
 */
		if (rbase > base) {
			nr_new++;
			if (insert)
				memblock_insert_region(type, i++, base,
						       rbase - base, nid,
						       flags);
		}
		/* area below @rend is dealt with, forget about it */
		base = min(rend, end);
	}


/* IAMROOT-12AB:
 * -------------
 * d) 요청 시작 주소(변경)가 끝 주소 보다 작은 경우
 */
	/* insert the remaining portion */
	if (base < end) {
		nr_new++;
		if (insert)
			memblock_insert_region(type, i, base, end - base,
					       nid, flags);
	}

	/*
	 * If this was the first round, resize array and repeat for actual
	 * insertions; otherwise, merge and return.
	 */

/* IAMROOT-12AB:
 * -------------
 * 기존 memblock region 최대 갯 수를 초과하는 경우 배열을 2배로 확장한다.
 */
	if (!insert) {
		while (type->cnt + nr_new > type->max)
			if (memblock_double_array(type, obase, size) < 0)
				return -ENOMEM;
		insert = true;
		goto repeat;
	} else {
		memblock_merge_regions(type);
		return 0;
	}
}

int __init_memblock memblock_add_node(phys_addr_t base, phys_addr_t size,
				       int nid)
{
	return memblock_add_range(&memblock.memory, base, size, nid, 0);
}


/* IAMROOT-12AB:
 * -------------
 * memory memblock에 메모리 영역을 추가
 * rpi2: base=0x0, size=0x4000_0000
 */

int __init_memblock memblock_add(phys_addr_t base, phys_addr_t size)
{
	return memblock_add_range(&memblock.memory, base, size,
				   MAX_NUMNODES, 0);
}

/**
 * memblock_isolate_range - isolate given range into disjoint memblocks
 * @type: memblock type to isolate range for
 * @base: base of range to isolate
 * @size: size of range to isolate
 * @start_rgn: out parameter for the start of isolated region
 * @end_rgn: out parameter for the end of isolated region
 *
 * Walk @type and ensure that regions don't cross the boundaries defined by
 * [@base,@base+@size).  Crossing regions are split at the boundaries,
 * which may create at most two more regions.  The index of the first
 * region inside the range is returned in *@start_rgn and end in *@end_rgn.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
static int __init_memblock memblock_isolate_range(struct memblock_type *type,
					phys_addr_t base, phys_addr_t size,
					int *start_rgn, int *end_rgn)
{
	phys_addr_t end = base + memblock_cap_size(base, &size);
	int i;

	*start_rgn = *end_rgn = 0;

	if (!size)
		return 0;

/* IAMROOT-12AB:
 * -------------
 * isloation을 하면 최대 2개의 메모리 블럭이 추가될 수 있다.
 */
	/* we'll create at most two more regions */
	while (type->cnt + 2 > type->max)
		if (memblock_double_array(type, base, size) < 0)
			return -ENOMEM;

	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		phys_addr_t rbase = rgn->base;
		phys_addr_t rend = rbase + rgn->size;


/* IAMROOT-12AB:
 * -------------
 * A) 분리할 영역이 메모리 블럭보다 하단에 위치한 경우 더 이상 진행할 필요 없다.
 */
		if (rbase >= end)
			break;

/* IAMROOT-12AB:
 * -------------
 * B) 분리할 영역이 메모리 블럭보다 상위에 위치한 경우 다음 메모리 블럭으로 진행한다.
 */
		if (rend <= base)
			continue;


/* IAMROOT-12AB:
 * -------------
 * C) 분리할 영역이 메모리 블럭의 위쪽에 겹칠때
 */
		if (rbase < base) {
			/*
			 * @rgn intersects from below.  Split and continue
			 * to process the next region - the new top half.
			 */
			rgn->base = base;
			rgn->size -= base - rbase;
			type->total_size -= base - rbase;
			memblock_insert_region(type, i, rbase, base - rbase,
					       memblock_get_region_node(rgn),
					       rgn->flags);

/* IAMROOT-12AB:
 * -------------
 * D) 분리할 영역이 메모리 블럭의 아래쪽에 겹칠때
 */
		} else if (rend > end) {
			/*
			 * @rgn intersects from above.  Split and redo the
			 * current region - the new bottom half.
			 */
			rgn->base = end;
			rgn->size -= end - rbase;
			type->total_size -= end - rbase;
			memblock_insert_region(type, i--, rbase, end - rbase,
					       memblock_get_region_node(rgn),
					       rgn->flags);

/* IAMROOT-12AB:
 * -------------
 * E) 분리할 영역이 메모리 블럭을 완전히 포함할 때는 아무것도 수행하지 않는다.
 *    start_rgn: 포함된 메모리 블럭의 시작 값
 *    end_rgn:   포함된 메모리 블럭의 마지막 값+1
 */
		} else {
			/* @rgn is fully contained, record it */
			if (!*end_rgn)
				*start_rgn = i;
			*end_rgn = i + 1;
		}
	}

	return 0;
}

int __init_memblock memblock_remove_range(struct memblock_type *type,
					  phys_addr_t base, phys_addr_t size)
{
	int start_rgn, end_rgn;
	int i, ret;

/* IAMROOT-12AB:
 * -------------
 * isloation 동작에서 최대 2개까지 memblock이 추가될 수 있다.
 */
	ret = memblock_isolate_range(type, base, size, &start_rgn, &end_rgn);
	if (ret)
		return ret;

/* IAMROOT-12AB:
 * -------------
 * 지정한 삭제 영역에 포함된 memblock을 제거한다.
 */
	for (i = end_rgn - 1; i >= start_rgn; i--)
		memblock_remove_region(type, i);
	return 0;
}


/* IAMROOT-12AB:
 * -------------
 * memory memblock의 주어진 영역을 삭제한다.
 */
int __init_memblock memblock_remove(phys_addr_t base, phys_addr_t size)
{
	return memblock_remove_range(&memblock.memory, base, size);
}


int __init_memblock memblock_free(phys_addr_t base, phys_addr_t size)
{
	memblock_dbg("   memblock_free: [%#016llx-%#016llx] %pF\n",
		     (unsigned long long)base,
		     (unsigned long long)base + size - 1,
		     (void *)_RET_IP_);

	kmemleak_free_part(__va(base), size);
	return memblock_remove_range(&memblock.reserved, base, size);
}

static int __init_memblock memblock_reserve_region(phys_addr_t base,
						   phys_addr_t size,
						   int nid,
						   unsigned long flags)
{
	struct memblock_type *_rgn = &memblock.reserved;

	memblock_dbg("memblock_reserve: [%#016llx-%#016llx] flags %#02lx %pF\n",
		     (unsigned long long)base,
		     (unsigned long long)base + size - 1,
		     flags, (void *)_RET_IP_);

	return memblock_add_range(_rgn, base, size, nid, flags);
}

int __init_memblock memblock_reserve(phys_addr_t base, phys_addr_t size)
{

/* IAMROOT-12AB:
 * -------------
 * flags: 0=normal, 1=MEMBLOCK_HOTPLUG
 */
	return memblock_reserve_region(base, size, MAX_NUMNODES, 0);
}

/**
 *
 * This function isolates region [@base, @base + @size), and sets/clears flag
 *
 * Return 0 on succees, -errno on failure.
 */

/* IAMROOT-12AB:
 * -------------
 * 지정된 영역의 flag를 변경
 *	- 먼저 지정된 영역을 isolation한 후
 *	- 플래그를 설정/해제 하고 
 *	- memblock을 merge한다.(flag가 같은 노드 메모리는 통합 가능)
 */

static int __init_memblock memblock_setclr_flag(phys_addr_t base,
				phys_addr_t size, int set, int flag)
{
	struct memblock_type *type = &memblock.memory;
	int i, ret, start_rgn, end_rgn;

	ret = memblock_isolate_range(type, base, size, &start_rgn, &end_rgn);
	if (ret)
		return ret;

	for (i = start_rgn; i < end_rgn; i++)
		if (set)
			memblock_set_region_flags(&type->regions[i], flag);
		else
			memblock_clear_region_flags(&type->regions[i], flag);

	memblock_merge_regions(type);
	return 0;
}

/**
 * memblock_mark_hotplug - Mark hotpluggable memory with flag MEMBLOCK_HOTPLUG.
 * @base: the base phys addr of the region
 * @size: the size of the region
 *
 * Return 0 on succees, -errno on failure.
 */
int __init_memblock memblock_mark_hotplug(phys_addr_t base, phys_addr_t size)
{
	return memblock_setclr_flag(base, size, 1, MEMBLOCK_HOTPLUG);
}

/**
 * memblock_clear_hotplug - Clear flag MEMBLOCK_HOTPLUG for a specified region.
 * @base: the base phys addr of the region
 * @size: the size of the region
 *
 * Return 0 on succees, -errno on failure.
 */

/* IAMROOT-12AB:
 * -------------
 * 지정된 영역의 MEMBLOCK_HOTPLUG 플래그를 제거한다.
 */
int __init_memblock memblock_clear_hotplug(phys_addr_t base, phys_addr_t size)
{
	return memblock_setclr_flag(base, size, 0, MEMBLOCK_HOTPLUG);
}

/**
 * __next__mem_range - next function for for_each_free_mem_range() etc.
 * @idx: pointer to u64 loop variable
 * @nid: node selector, %NUMA_NO_NODE for all nodes
 * @type_a: pointer to memblock_type from where the range is taken
 * @type_b: pointer to memblock_type which excludes memory from being taken
 * @out_start: ptr to phys_addr_t for start address of the range, can be %NULL
 * @out_end: ptr to phys_addr_t for end address of the range, can be %NULL
 * @out_nid: ptr to int for nid of the range, can be %NULL
 *
 * Find the first area from *@idx which matches @nid, fill the out
 * parameters, and update *@idx for the next iteration.  The lower 32bit of
 * *@idx contains index into type_a and the upper 32bit indexes the
 * areas before each region in type_b.	For example, if type_b regions
 * look like the following,
 *
 *	0:[0-16), 1:[32-48), 2:[128-130)
 *
 * The upper 32bit indexes the following regions.
 *
 *	0:[0-0), 1:[16-32), 2:[48-128), 3:[130-MAX)
 *
 * As both region arrays are sorted, the function advances the two indices
 * in lockstep and returns each intersection.
 */
void __init_memblock __next_mem_range(u64 *idx, int nid,
				      struct memblock_type *type_a,
				      struct memblock_type *type_b,
				      phys_addr_t *out_start,
				      phys_addr_t *out_end, int *out_nid)
{
	int idx_a = *idx & 0xffffffff;
	int idx_b = *idx >> 32;

	if (WARN_ONCE(nid == MAX_NUMNODES,
	"Usage of MAX_NUMNODES is deprecated. Use NUMA_NO_NODE instead\n"))
		nid = NUMA_NO_NODE;

	for (; idx_a < type_a->cnt; idx_a++) {
		struct memblock_region *m = &type_a->regions[idx_a];

		phys_addr_t m_start = m->base;
		phys_addr_t m_end = m->base + m->size;
		int	    m_nid = memblock_get_region_node(m);

		/* only memory regions are associated with nodes, check it */
		if (nid != NUMA_NO_NODE && nid != m_nid)
			continue;

		/* skip hotpluggable memory regions if needed */
		if (movable_node_is_enabled() && memblock_is_hotpluggable(m))
			continue;

		if (!type_b) {
			if (out_start)
				*out_start = m_start;
			if (out_end)
				*out_end = m_end;
			if (out_nid)
				*out_nid = m_nid;
			idx_a++;
			*idx = (u32)idx_a | (u64)idx_b << 32;
			return;
		}

		/* scan areas before each reservation */
		for (; idx_b < type_b->cnt + 1; idx_b++) {
			struct memblock_region *r;
			phys_addr_t r_start;
			phys_addr_t r_end;

			r = &type_b->regions[idx_b];
			r_start = idx_b ? r[-1].base + r[-1].size : 0;
			r_end = idx_b < type_b->cnt ?
				r->base : ULLONG_MAX;

			/*
			 * if idx_b advanced past idx_a,
			 * break out to advance idx_a
			 */
			if (r_start >= m_end)
				break;
			/* if the two regions intersect, we're done */
			if (m_start < r_end) {
				if (out_start)
					*out_start =
						max(m_start, r_start);
				if (out_end)
					*out_end = min(m_end, r_end);
				if (out_nid)
					*out_nid = m_nid;
				/*
				 * The region which ends first is
				 * advanced for the next iteration.
				 */
				if (m_end <= r_end)
					idx_a++;
				else
					idx_b++;
				*idx = (u32)idx_a | (u64)idx_b << 32;
				return;
			}
		}
	}

	/* signal end of iteration */
	*idx = ULLONG_MAX;
}

/**
 * __next_mem_range_rev - generic next function for for_each_*_range_rev()
 *
 * Finds the next range from type_a which is not marked as unsuitable
 * in type_b.
 *
 * @idx: pointer to u64 loop variable
 * @nid: nid: node selector, %NUMA_NO_NODE for all nodes
 * @type_a: pointer to memblock_type from where the range is taken
 * @type_b: pointer to memblock_type which excludes memory from being taken
 * @out_start: ptr to phys_addr_t for start address of the range, can be %NULL
 * @out_end: ptr to phys_addr_t for end address of the range, can be %NULL
 * @out_nid: ptr to int for nid of the range, can be %NULL
 *
 * Reverse of __next_mem_range().
 */
void __init_memblock __next_mem_range_rev(u64 *idx, int nid,
					  struct memblock_type *type_a,
					  struct memblock_type *type_b,
					  phys_addr_t *out_start,
					  phys_addr_t *out_end, int *out_nid)
{

/* IAMROOT-12AB:
 * -------------
 * idx가 64비트 값으로 이루어지며 idx_a = msb 32bits, idx_b = lsb 32bits
 */
	int idx_a = *idx & 0xffffffff;
	int idx_b = *idx >> 32;

	if (WARN_ONCE(nid == MAX_NUMNODES, "Usage of MAX_NUMNODES is deprecated. Use NUMA_NO_NODE instead\n"))
		nid = NUMA_NO_NODE;


/* IAMROOT-12AB:
 * -------------
 * idx값이 처음 진입하면 ULLONG_MAX 값이다.
 */
	if (*idx == (u64)ULLONG_MAX) {
		idx_a = type_a->cnt - 1;
		idx_b = type_b->cnt;
	}

/* IAMROOT-12AB:
 * -------------
 * idx_a: memory 쪽 카운터
 * idx_b: reserve 쪽 카운터
 */
	for (; idx_a >= 0; idx_a--) {
		struct memblock_region *m = &type_a->regions[idx_a];

		phys_addr_t m_start = m->base;
		phys_addr_t m_end = m->base + m->size;
		int m_nid = memblock_get_region_node(m);


/* IAMROOT-12AB:
 * -------------
 * 검색한 블럭이 요청한 노드 id가 아닌 경우 continue
 */
		/* only memory regions are associated with nodes, check it */
		if (nid != NUMA_NO_NODE && nid != m_nid)
			continue;

		/* skip hotpluggable memory regions if needed */
		if (movable_node_is_enabled() && memblock_is_hotpluggable(m))
			continue;


/* IAMROOT-12AB:
 * -------------
 * reserve 타입을 지정하지 않은 경우 memory에서 찾은 영역을 그대로 return
 */
		if (!type_b) {
			if (out_start)
				*out_start = m_start;
			if (out_end)
				*out_end = m_end;
			if (out_nid)
				*out_nid = m_nid;
			idx_a++;
			*idx = (u32)idx_a | (u64)idx_b << 32;
			return;
		}

		/* scan areas before each reservation */
		for (; idx_b >= 0; idx_b--) {
			struct memblock_region *r;
			phys_addr_t r_start;
			phys_addr_t r_end;

			r = &type_b->regions[idx_b];
			r_start = idx_b ? r[-1].base + r[-1].size : 0;
			r_end = idx_b < type_b->cnt ?
				r->base : ULLONG_MAX;
			/*
			 * if idx_b advanced past idx_a,
			 * break out to advance idx_a
			 */

			if (r_end <= m_start)
				break;
			/* if the two regions intersect, we're done */
			if (m_end > r_start) {
				if (out_start)
					*out_start = max(m_start, r_start);
				if (out_end)
					*out_end = min(m_end, r_end);
				if (out_nid)
					*out_nid = m_nid;

/* IAMROOT-12AB:
 * -------------
 * 검색한 영역의 시작 주소가 memory 영역을 아래쪽으로 벗어나면
 * 다음 메모리 영역을 사용하기 위해 idx_a-- 한다.
 * 그렇지 않은 경우는 아직 region 영역을 검색하기 위해 idx_b--한다.
 */
				if (m_start >= r_start)
					idx_a--;
				else
					idx_b--;
				*idx = (u32)idx_a | (u64)idx_b << 32;
				return;
			}
		}
	}
	/* signal end of iteration */
	*idx = ULLONG_MAX;
}

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
/*
 * Common iterator interface used to define for_each_mem_range().
 */
void __init_memblock __next_mem_pfn_range(int *idx, int nid,
				unsigned long *out_start_pfn,
				unsigned long *out_end_pfn, int *out_nid)
{
	struct memblock_type *type = &memblock.memory;
	struct memblock_region *r;

	while (++*idx < type->cnt) {
		r = &type->regions[*idx];


/* IAMROOT-12AB:
 * -------------
 * 영역이 하나도 온전한 페이지를 포함하지 않으면 다음 블럭으로 진행
 */
		if (PFN_UP(r->base) >= PFN_DOWN(r->base + r->size))
			continue;

/* IAMROOT-12AB:
 * -------------
 * 지정된 노드 번호인 경우
 */
		if (nid == MAX_NUMNODES || nid == r->nid)
			break;
	}

/* IAMROOT-12AB:
 * -------------
 * 모든 memory memblock에서 원하는 영역(온전한 한페이지 이상을 갖은)이
 * 발견되지 않으면
 */
	if (*idx >= type->cnt) {
		*idx = -1;
		return;
	}

	if (out_start_pfn)
		*out_start_pfn = PFN_UP(r->base);
	if (out_end_pfn)
		*out_end_pfn = PFN_DOWN(r->base + r->size);
	if (out_nid)
		*out_nid = r->nid;
}

/**
 * memblock_set_node - set node ID on memblock regions
 * @base: base of area to set node ID for
 * @size: size of area to set node ID for
 * @type: memblock type to set node ID for
 * @nid: node ID to set
 *
 * Set the nid of memblock @type regions in [@base,@base+@size) to @nid.
 * Regions which cross the area boundaries are split as necessary.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init_memblock memblock_set_node(phys_addr_t base, phys_addr_t size,
				      struct memblock_type *type, int nid)
{
	int start_rgn, end_rgn;
	int i, ret;

	ret = memblock_isolate_range(type, base, size, &start_rgn, &end_rgn);
	if (ret)
		return ret;

	for (i = start_rgn; i < end_rgn; i++)
		memblock_set_region_node(&type->regions[i], nid);

	memblock_merge_regions(type);
	return 0;
}
#endif /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */

static phys_addr_t __init memblock_alloc_range_nid(phys_addr_t size,
					phys_addr_t align, phys_addr_t start,
					phys_addr_t end, int nid)
{
	phys_addr_t found;

/* IAMROOT-12AB:
 * -------------
 * 특별히 align 지정이 없는 경우 cpu 캐시 라인 크기에 맞게 정렬
 * rpi2: 64 byte 단위
 */
	if (!align)
		align = SMP_CACHE_BYTES;

	found = memblock_find_in_range_node(size, align, start, end, nid);
	if (found && !memblock_reserve(found, size)) {
		/*
		 * The min_count is set to 0 so that memblock allocations are
		 * never reported as leaks.
		 */

/* IAMROOT-12AB:
 * -------------
 * allocation이 성공되어 memblock의 시작 주소가 리턴된다.
 */
		kmemleak_alloc(__va(found), size, 0, 0);
		return found;
	}
	return 0;
}

phys_addr_t __init memblock_alloc_range(phys_addr_t size, phys_addr_t align,
					phys_addr_t start, phys_addr_t end)
{
	return memblock_alloc_range_nid(size, align, start, end, NUMA_NO_NODE);
}

static phys_addr_t __init memblock_alloc_base_nid(phys_addr_t size,
					phys_addr_t align, phys_addr_t max_addr,
					int nid)
{
	return memblock_alloc_range_nid(size, align, 0, max_addr, nid);
}

phys_addr_t __init memblock_alloc_nid(phys_addr_t size, phys_addr_t align, int nid)
{
	return memblock_alloc_base_nid(size, align, MEMBLOCK_ALLOC_ACCESSIBLE, nid);
}

phys_addr_t __init __memblock_alloc_base(phys_addr_t size, phys_addr_t align, phys_addr_t max_addr)
{

/* IAMROOT-12AB:
 * -------------
 * NUMA_NO_NODE: memory memblock의 어떠한 노드라도 상관 없을 때 사용
 */
	return memblock_alloc_base_nid(size, align, max_addr, NUMA_NO_NODE);
}

phys_addr_t __init memblock_alloc_base(phys_addr_t size, phys_addr_t align, phys_addr_t max_addr)
{
	phys_addr_t alloc;

	alloc = __memblock_alloc_base(size, align, max_addr);

	if (alloc == 0)
		panic("ERROR: Failed to allocate 0x%llx bytes below 0x%llx.\n",
		      (unsigned long long) size, (unsigned long long) max_addr);

	return alloc;
}

phys_addr_t __init memblock_alloc(phys_addr_t size, phys_addr_t align)
{
	return memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ACCESSIBLE);
}

phys_addr_t __init memblock_alloc_try_nid(phys_addr_t size, phys_addr_t align, int nid)
{
	phys_addr_t res = memblock_alloc_nid(size, align, nid);

	if (res)
		return res;
	return memblock_alloc_base(size, align, MEMBLOCK_ALLOC_ACCESSIBLE);
}

/**
 * memblock_virt_alloc_internal - allocate boot memory block
 * @size: size of memory block to be allocated in bytes
 * @align: alignment of the region and block's size
 * @min_addr: the lower bound of the memory region to allocate (phys address)
 * @max_addr: the upper bound of the memory region to allocate (phys address)
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * The @min_addr limit is dropped if it can not be satisfied and the allocation
 * will fall back to memory below @min_addr. Also, allocation may fall back
 * to any node in the system if the specified node can not
 * hold the requested memory.
 *
 * The allocation is performed from memory region limited by
 * memblock.current_limit if @max_addr == %BOOTMEM_ALLOC_ACCESSIBLE.
 *
 * The memory block is aligned on SMP_CACHE_BYTES if @align == 0.
 *
 * The phys address of allocated boot memory block is converted to virtual and
 * allocated memory is reset to 0.
 *
 * In addition, function sets the min_count to 0 using kmemleak_alloc for
 * allocated boot memory block, so that it is never reported as leaks.
 *
 * RETURNS:
 * Virtual address of allocated memory block on success, NULL on failure.
 */
static void * __init memblock_virt_alloc_internal(
				phys_addr_t size, phys_addr_t align,
				phys_addr_t min_addr, phys_addr_t max_addr,
				int nid)
{
	phys_addr_t alloc;
	void *ptr;

	if (WARN_ONCE(nid == MAX_NUMNODES, "Usage of MAX_NUMNODES is deprecated. Use NUMA_NO_NODE instead\n"))
		nid = NUMA_NO_NODE;

	/*
	 * Detect any accidental use of these APIs after slab is ready, as at
	 * this moment memblock may be deinitialized already and its
	 * internal data may be destroyed (after execution of free_all_bootmem)
	 */
	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, nid);

/* IAMROOT-12AB:
 * -------------
 * align=0인 경우 L1 캐시 사이즈만큼 align한다. (rpi2: 64 bytes)
 */
	if (!align)
		align = SMP_CACHE_BYTES;

	if (max_addr > memblock.current_limit)
		max_addr = memblock.current_limit;

again:

/* IAMROOT-12AB:
 * -------------
 * 1st round: 지정된 노드의 지정된 범위에서 free 영역을 검색
 * 2nd round: 지정된 노드의 0~max_addr 범위에서 free 영역을 검색
 */
	alloc = memblock_find_in_range_node(size, align, min_addr, max_addr,
					    nid);
	if (alloc)
		goto done;

/* IAMROOT-12AB:
 * -------------
 * 1st round: 노드에 관계없이 지정된 범위에서 free 영역을 검색
 * 2nd round: 노드에 관계없이 0~max_addr 범위에서 free 영역을 검색
 */
	if (nid != NUMA_NO_NODE) {
		alloc = memblock_find_in_range_node(size, align, min_addr,
						    max_addr,  NUMA_NO_NODE);
		if (alloc)
			goto done;
	}

/* IAMROOT-12AB:
 * -------------
 * 검색 범위로 min_addr가 지정되어 있는 경우 min_addr=0으로 하고 다시 시도
 */
	if (min_addr) {
		min_addr = 0;
		goto again;
	} else {
		goto error;
	}

done:

/* IAMROOT-12AB:
 * -------------
 * 찾은 영역을 할당받고 0으로 클리어한다.
 * 할당 받은 주소는 가상주소를 반환한다.
 */

	memblock_reserve(alloc, size);
	ptr = phys_to_virt(alloc);
	memset(ptr, 0, size);

	/*
	 * The min_count is set to 0 so that bootmem allocated blocks
	 * are never reported as leaks. This is because many of these blocks
	 * are only referred via the physical address which is not
	 * looked up by kmemleak.
	 */
	kmemleak_alloc(ptr, size, 0, 0);

	return ptr;

error:
	return NULL;
}

/**
 * memblock_virt_alloc_try_nid_nopanic - allocate boot memory block
 * @size: size of memory block to be allocated in bytes
 * @align: alignment of the region and block's size
 * @min_addr: the lower bound of the memory region from where the allocation
 *	  is preferred (phys address)
 * @max_addr: the upper bound of the memory region from where the allocation
 *	      is preferred (phys address), or %BOOTMEM_ALLOC_ACCESSIBLE to
 *	      allocate only from memory limited by memblock.current_limit value
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * Public version of _memblock_virt_alloc_try_nid_nopanic() which provides
 * additional debug information (including caller info), if enabled.
 *
 * RETURNS:
 * Virtual address of allocated memory block on success, NULL on failure.
 */
/* IAMROOT-12 fehead (2017-01-02):
 * --------------------------
 * pi2
 */
void * __init memblock_virt_alloc_try_nid_nopanic(
				phys_addr_t size, phys_addr_t align,
				phys_addr_t min_addr, phys_addr_t max_addr,
				int nid)
{
	memblock_dbg("%s: %llu bytes align=0x%llx nid=%d from=0x%llx max_addr=0x%llx %pF\n",
		     __func__, (u64)size, (u64)align, nid, (u64)min_addr,
		     (u64)max_addr, (void *)_RET_IP_);

/* IAMROOT-12AB:
 * -------------
 * 최대 4번의 검색을 통해 할당되지 않는 경우 null을 리턴한다.
 * (UMA를 사용하는 arm에서는 free 메모리 검색을 1회 한다.)
 */
	return memblock_virt_alloc_internal(size, align, min_addr,
					     max_addr, nid);
}

/**
 * memblock_virt_alloc_try_nid - allocate boot memory block with panicking
 * @size: size of memory block to be allocated in bytes
 * @align: alignment of the region and block's size
 * @min_addr: the lower bound of the memory region from where the allocation
 *	  is preferred (phys address)
 * @max_addr: the upper bound of the memory region from where the allocation
 *	      is preferred (phys address), or %BOOTMEM_ALLOC_ACCESSIBLE to
 *	      allocate only from memory limited by memblock.current_limit value
 * @nid: nid of the free area to find, %NUMA_NO_NODE for any node
 *
 * Public panicking version of _memblock_virt_alloc_try_nid_nopanic()
 * which provides debug information (including caller info), if enabled,
 * and panics if the request can not be satisfied.
 *
 * RETURNS:
 * Virtual address of allocated memory block on success, NULL on failure.
 */
void * __init memblock_virt_alloc_try_nid(
			phys_addr_t size, phys_addr_t align,
			phys_addr_t min_addr, phys_addr_t max_addr,
			int nid)
{
	void *ptr;

	memblock_dbg("%s: %llu bytes align=0x%llx nid=%d from=0x%llx max_addr=0x%llx %pF\n",
		     __func__, (u64)size, (u64)align, nid, (u64)min_addr,
		     (u64)max_addr, (void *)_RET_IP_);

/* IAMROOT-12AB:
 * -------------
 * 메모리를 memblock에서 할당받고 할당받은 가상 주소를 반환한다.
 * 할당 순서:
 *	지정된 노드와 범위 -> 노드관계없이 지정된 범위 -> 
 *	지정된 노드와 0~max 범위 -> 노드관계없이 0~max 범위로 할당을 받아온다.
 */
	ptr = memblock_virt_alloc_internal(size, align,
					   min_addr, max_addr, nid);
	if (ptr)
		return ptr;

	panic("%s: Failed to allocate %llu bytes align=0x%llx nid=%d from=0x%llx max_addr=0x%llx\n",
	      __func__, (u64)size, (u64)align, nid, (u64)min_addr,
	      (u64)max_addr);
	return NULL;
}

/**
 * __memblock_free_early - free boot memory block
 * @base: phys starting address of the  boot memory block
 * @size: size of the boot memory block in bytes
 *
 * Free boot memory block previously allocated by memblock_virt_alloc_xx() API.
 * The freeing memory will not be released to the buddy allocator.
 */
void __init __memblock_free_early(phys_addr_t base, phys_addr_t size)
{
	memblock_dbg("%s: [%#016llx-%#016llx] %pF\n",
		     __func__, (u64)base, (u64)base + size - 1,
		     (void *)_RET_IP_);
	kmemleak_free_part(__va(base), size);
	memblock_remove_range(&memblock.reserved, base, size);
}

/*
 * __memblock_free_late - free bootmem block pages directly to buddy allocator
 * @addr: phys starting address of the  boot memory block
 * @size: size of the boot memory block in bytes
 *
 * This is only useful when the bootmem allocator has already been torn
 * down, but we are still initializing the system.  Pages are released directly
 * to the buddy allocator, no bootmem metadata is updated because it is gone.
 */
void __init __memblock_free_late(phys_addr_t base, phys_addr_t size)
{
	u64 cursor, end;

	memblock_dbg("%s: [%#016llx-%#016llx] %pF\n",
		     __func__, (u64)base, (u64)base + size - 1,
		     (void *)_RET_IP_);
	kmemleak_free_part(__va(base), size);
	cursor = PFN_UP(base);
	end = PFN_DOWN(base + size);

	for (; cursor < end; cursor++) {
		__free_pages_bootmem(pfn_to_page(cursor), 0);
		totalram_pages++;
	}
}

/*
 * Remaining API functions
 */

phys_addr_t __init memblock_phys_mem_size(void)
{
	return memblock.memory.total_size;
}

phys_addr_t __init memblock_mem_size(unsigned long limit_pfn)
{
	unsigned long pages = 0;
	struct memblock_region *r;
	unsigned long start_pfn, end_pfn;

	for_each_memblock(memory, r) {
		start_pfn = memblock_region_memory_base_pfn(r);
		end_pfn = memblock_region_memory_end_pfn(r);
		start_pfn = min_t(unsigned long, start_pfn, limit_pfn);
		end_pfn = min_t(unsigned long, end_pfn, limit_pfn);
		pages += end_pfn - start_pfn;
	}

	return PFN_PHYS(pages);
}

/* lowest address */
phys_addr_t __init_memblock memblock_start_of_DRAM(void)
{
	return memblock.memory.regions[0].base;
}

phys_addr_t __init_memblock memblock_end_of_DRAM(void)
{
	int idx = memblock.memory.cnt - 1;

	return (memblock.memory.regions[idx].base + memblock.memory.regions[idx].size);
}

void __init memblock_enforce_memory_limit(phys_addr_t limit)
{
	phys_addr_t max_addr = (phys_addr_t)ULLONG_MAX;
	struct memblock_region *r;

	if (!limit)
		return;

	/* find out max address */
	for_each_memblock(memory, r) {
		if (limit <= r->size) {
			max_addr = r->base + limit;
			break;
		}
		limit -= r->size;
	}

	/* truncate both memory and reserved regions */
	memblock_remove_range(&memblock.memory, max_addr,
			      (phys_addr_t)ULLONG_MAX);
	memblock_remove_range(&memblock.reserved, max_addr,
			      (phys_addr_t)ULLONG_MAX);
}


/* IAMROOT-12AB:
 * -------------
 * binary search에 의해 addr이 포함된 memblock의 index를 찾는다.
 */
static int __init_memblock memblock_search(struct memblock_type *type, phys_addr_t addr)
{
	unsigned int left = 0, right = type->cnt;

	do {
		unsigned int mid = (right + left) / 2;

		if (addr < type->regions[mid].base)
			right = mid;
		else if (addr >= (type->regions[mid].base +
				  type->regions[mid].size))
			left = mid + 1;
		else
			return mid;
	} while (left < right);
	return -1;
}

int __init memblock_is_reserved(phys_addr_t addr)
{
	return memblock_search(&memblock.reserved, addr) != -1;
}

int __init_memblock memblock_is_memory(phys_addr_t addr)
{
	return memblock_search(&memblock.memory, addr) != -1;
}

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
int __init_memblock memblock_search_pfn_nid(unsigned long pfn,
			 unsigned long *start_pfn, unsigned long *end_pfn)
{
	struct memblock_type *type = &memblock.memory;
	int mid = memblock_search(type, PFN_PHYS(pfn));

	if (mid == -1)
		return -1;

	*start_pfn = PFN_DOWN(type->regions[mid].base);
	*end_pfn = PFN_DOWN(type->regions[mid].base + type->regions[mid].size);

	return type->regions[mid].nid;
}
#endif

/**
 * memblock_is_region_memory - check if a region is a subset of memory
 * @base: base of region to check
 * @size: size of region to check
 *
 * Check if the region [@base, @base+@size) is a subset of a memory block.
 *
 * RETURNS:
 * 0 if false, non-zero if true
 */
int __init_memblock memblock_is_region_memory(phys_addr_t base, phys_addr_t size)
{

/* IAMROOT-12AB:
 * -------------
 * base가 포함된 memory memblock의 인덱스를 알아온다.
 */
	int idx = memblock_search(&memblock.memory, base);
	phys_addr_t end = base + memblock_cap_size(base, &size);

	if (idx == -1)
		return 0;

/* IAMROOT-12AB:
 * -------------
 * 영역 전체가 포함된 경우 true를 리턴한다.
 */
	return memblock.memory.regions[idx].base <= base &&
		(memblock.memory.regions[idx].base +
		 memblock.memory.regions[idx].size) >= end;
}

/**
 * memblock_is_region_reserved - check if a region intersects reserved memory
 * @base: base of region to check
 * @size: size of region to check
 *
 * Check if the region [@base, @base+@size) intersects a reserved memory block.
 *
 * RETURNS:
 * 0 if false, non-zero if true
 */
int __init_memblock memblock_is_region_reserved(phys_addr_t base, phys_addr_t size)
{
	memblock_cap_size(base, &size);

/* IAMROOT-12AB:
 * -------------
 * memblock이 지정된 영역과 일부라도 겹쳐있는 경우 true
 */
	return memblock_overlaps_region(&memblock.reserved, base, size) >= 0;
}

void __init_memblock memblock_trim_memory(phys_addr_t align)
{
	phys_addr_t start, end, orig_start, orig_end;
	struct memblock_region *r;

	for_each_memblock(memory, r) {
		orig_start = r->base;
		orig_end = r->base + r->size;
		start = round_up(orig_start, align);
		end = round_down(orig_end, align);

		if (start == orig_start && end == orig_end)
			continue;

		if (start < end) {
			r->base = start;
			r->size = end - start;
		} else {
			memblock_remove_region(&memblock.memory,
					       r - memblock.memory.regions);
			r--;
		}
	}
}

void __init_memblock memblock_set_current_limit(phys_addr_t limit)
{
	memblock.current_limit = limit;
}

phys_addr_t __init_memblock memblock_get_current_limit(void)
{
	return memblock.current_limit;
}

static void __init_memblock memblock_dump(struct memblock_type *type, char *name)
{
	unsigned long long base, size;
	unsigned long flags;
	int i;

	pr_info(" %s.cnt  = 0x%lx\n", name, type->cnt);

	for (i = 0; i < type->cnt; i++) {
		struct memblock_region *rgn = &type->regions[i];
		char nid_buf[32] = "";

		base = rgn->base;
		size = rgn->size;
		flags = rgn->flags;
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
		if (memblock_get_region_node(rgn) != MAX_NUMNODES)
			snprintf(nid_buf, sizeof(nid_buf), " on node %d",
				 memblock_get_region_node(rgn));
#endif
		pr_info(" %s[%#x]\t[%#016llx-%#016llx], %#llx bytes%s flags: %#lx\n",
			name, i, base, base + size - 1, size, nid_buf, flags);
	}
}

void __init_memblock __memblock_dump_all(void)
{
	pr_info("MEMBLOCK configuration:\n");
	pr_info(" memory size = %#llx reserved size = %#llx\n",
		(unsigned long long)memblock.memory.total_size,
		(unsigned long long)memblock.reserved.total_size);

	memblock_dump(&memblock.memory, "memory");
	memblock_dump(&memblock.reserved, "reserved");
}

void __init memblock_allow_resize(void)
{
	memblock_can_resize = 1;
}

static int __init early_memblock(char *p)
{
	if (p && strstr(p, "debug"))
		memblock_debug = 1;
	return 0;
}
early_param("memblock", early_memblock);

#if defined(CONFIG_DEBUG_FS) && !defined(CONFIG_ARCH_DISCARD_MEMBLOCK)

static int memblock_debug_show(struct seq_file *m, void *private)
{
	struct memblock_type *type = m->private;
	struct memblock_region *reg;
	int i;

	for (i = 0; i < type->cnt; i++) {
		reg = &type->regions[i];
		seq_printf(m, "%4d: ", i);
		if (sizeof(phys_addr_t) == 4)
			seq_printf(m, "0x%08lx..0x%08lx\n",
				   (unsigned long)reg->base,
				   (unsigned long)(reg->base + reg->size - 1));
		else
			seq_printf(m, "0x%016llx..0x%016llx\n",
				   (unsigned long long)reg->base,
				   (unsigned long long)(reg->base + reg->size - 1));

	}
	return 0;
}

static int memblock_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, memblock_debug_show, inode->i_private);
}

static const struct file_operations memblock_debug_fops = {
	.open = memblock_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init memblock_init_debugfs(void)
{
	struct dentry *root = debugfs_create_dir("memblock", NULL);
	if (!root)
		return -ENXIO;
	debugfs_create_file("memory", S_IRUGO, root, &memblock.memory, &memblock_debug_fops);
	debugfs_create_file("reserved", S_IRUGO, root, &memblock.reserved, &memblock_debug_fops);
#ifdef CONFIG_HAVE_MEMBLOCK_PHYS_MAP
	debugfs_create_file("physmem", S_IRUGO, root, &memblock.physmem, &memblock_debug_fops);
#endif

	return 0;
}
__initcall(memblock_init_debugfs);

#endif /* CONFIG_DEBUG_FS */
