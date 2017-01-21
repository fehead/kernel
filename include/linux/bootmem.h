/*
 * Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 */
#ifndef _LINUX_BOOTMEM_H
#define _LINUX_BOOTMEM_H

#include <linux/mmzone.h>
#include <linux/mm_types.h>
#include <asm/dma.h>

/*
 *  simple boot-time physical memory area allocator.
 */

extern unsigned long max_low_pfn;
extern unsigned long min_low_pfn;

/*
 * highest page
 */
extern unsigned long max_pfn;

#ifndef CONFIG_NO_BOOTMEM
/*
 * node_bootmem_map is a map pointer - the bits represent all physical 
 * memory pages (including holes) on the node.
 */
typedef struct bootmem_data {
	unsigned long node_min_pfn;
	unsigned long node_low_pfn;
	void *node_bootmem_map;
	unsigned long last_end_off;
	unsigned long hint_idx;
	struct list_head list;
} bootmem_data_t;

extern bootmem_data_t bootmem_node_data[];
#endif

extern unsigned long bootmem_bootmap_pages(unsigned long);

extern unsigned long init_bootmem_node(pg_data_t *pgdat,
				       unsigned long freepfn,
				       unsigned long startpfn,
				       unsigned long endpfn);
extern unsigned long init_bootmem(unsigned long addr, unsigned long memend);

extern unsigned long free_all_bootmem(void);
extern void reset_node_managed_pages(pg_data_t *pgdat);
extern void reset_all_zones_managed_pages(void);

extern void free_bootmem_node(pg_data_t *pgdat,
			      unsigned long addr,
			      unsigned long size);
extern void free_bootmem(unsigned long physaddr, unsigned long size);
extern void free_bootmem_late(unsigned long physaddr, unsigned long size);

/*
 * Flags for reserve_bootmem (also if CONFIG_HAVE_ARCH_BOOTMEM_NODE,
 * the architecture-specific code should honor this).
 *
 * If flags is BOOTMEM_DEFAULT, then the return value is always 0 (success).
 * If flags contains BOOTMEM_EXCLUSIVE, then -EBUSY is returned if the memory
 * already was reserved.
 */
#define BOOTMEM_DEFAULT		0
#define BOOTMEM_EXCLUSIVE	(1<<0)

extern int reserve_bootmem(unsigned long addr,
			   unsigned long size,
			   int flags);
extern int reserve_bootmem_node(pg_data_t *pgdat,
				unsigned long physaddr,
				unsigned long size,
				int flags);

extern void *__alloc_bootmem(unsigned long size,
			     unsigned long align,
			     unsigned long goal);
extern void *__alloc_bootmem_nopanic(unsigned long size,
				     unsigned long align,
				     unsigned long goal);
extern void *__alloc_bootmem_node(pg_data_t *pgdat,
				  unsigned long size,
				  unsigned long align,
				  unsigned long goal);
void *__alloc_bootmem_node_high(pg_data_t *pgdat,
				  unsigned long size,
				  unsigned long align,
				  unsigned long goal);
extern void *__alloc_bootmem_node_nopanic(pg_data_t *pgdat,
				  unsigned long size,
				  unsigned long align,
				  unsigned long goal);
void *___alloc_bootmem_node_nopanic(pg_data_t *pgdat,
				  unsigned long size,
				  unsigned long align,
				  unsigned long goal,
				  unsigned long limit);
extern void *__alloc_bootmem_low(unsigned long size,
				 unsigned long align,
				 unsigned long goal);
void *__alloc_bootmem_low_nopanic(unsigned long size,
				 unsigned long align,
				 unsigned long goal);
extern void *__alloc_bootmem_low_node(pg_data_t *pgdat,
				      unsigned long size,
				      unsigned long align,
				      unsigned long goal);

/* IAMROOT-12AB:
 * -------------
 * 최근의 arm은 bootmem을 사용하지 않는다.
 */
#ifdef CONFIG_NO_BOOTMEM
/* We are using top down, so it is safe to use 0 here */
/* IAMROOT-12 fehead (2017-01-02):
 * --------------------------
 * pi2
 */
#define BOOTMEM_LOW_LIMIT 0
#else
#define BOOTMEM_LOW_LIMIT __pa(MAX_DMA_ADDRESS)
#endif

#define alloc_bootmem(x) \
	__alloc_bootmem(x, SMP_CACHE_BYTES, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_align(x, align) \
	__alloc_bootmem(x, align, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_nopanic(x) \
	__alloc_bootmem_nopanic(x, SMP_CACHE_BYTES, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_pages(x) \
	__alloc_bootmem(x, PAGE_SIZE, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_pages_nopanic(x) \
	__alloc_bootmem_nopanic(x, PAGE_SIZE, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_node(pgdat, x) \
	__alloc_bootmem_node(pgdat, x, SMP_CACHE_BYTES, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_node_nopanic(pgdat, x) \
	__alloc_bootmem_node_nopanic(pgdat, x, SMP_CACHE_BYTES, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_pages_node(pgdat, x) \
	__alloc_bootmem_node(pgdat, x, PAGE_SIZE, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_pages_node_nopanic(pgdat, x) \
	__alloc_bootmem_node_nopanic(pgdat, x, PAGE_SIZE, BOOTMEM_LOW_LIMIT)

#define alloc_bootmem_low(x) \
	__alloc_bootmem_low(x, SMP_CACHE_BYTES, 0)
#define alloc_bootmem_low_pages_nopanic(x) \
	__alloc_bootmem_low_nopanic(x, PAGE_SIZE, 0)
#define alloc_bootmem_low_pages(x) \
	__alloc_bootmem_low(x, PAGE_SIZE, 0)
#define alloc_bootmem_low_pages_node(pgdat, x) \
	__alloc_bootmem_low_node(pgdat, x, PAGE_SIZE, 0)


/* IAMROOT-12 fehead (2016-11-28):
 * --------------------------
 * pi2
 */
#if defined(CONFIG_HAVE_MEMBLOCK) && defined(CONFIG_NO_BOOTMEM)

/* FIXME: use MEMBLOCK_ALLOC_* variants here */
#define BOOTMEM_ALLOC_ACCESSIBLE	0
#define BOOTMEM_ALLOC_ANYWHERE		(~(phys_addr_t)0)

/* FIXME: Move to memblock.h at a point where we remove nobootmem.c */
void *memblock_virt_alloc_try_nid_nopanic(phys_addr_t size,
		phys_addr_t align, phys_addr_t min_addr,
		phys_addr_t max_addr, int nid);
void *memblock_virt_alloc_try_nid(phys_addr_t size, phys_addr_t align,
		phys_addr_t min_addr, phys_addr_t max_addr, int nid);
void __memblock_free_early(phys_addr_t base, phys_addr_t size);
void __memblock_free_late(phys_addr_t base, phys_addr_t size);

/* IAMROOT-12 fehead (2016-11-19):
 * --------------------------
 * free 영역을 구한다음 memblock.reserved 영역에 할당한 메모리 정보를 넣고 물리
 * 주소를 가상주소로 바꾼후 반환한다.
 */
static inline void * __init memblock_virt_alloc(
					phys_addr_t size,  phys_addr_t align)
{

/* IAMROOT-12AB:
 * -------------
 * 메모리를 memblock에서 할당받고 할당받은 가상 주소를 반환한다.
 * 할당 순서:
 *	노드관계없이 지정된 범위 -> 
 *	노드관계없이 0~max 범위로 할당을 받아온다.
 */

	return memblock_virt_alloc_try_nid(size, align, BOOTMEM_LOW_LIMIT,
					    BOOTMEM_ALLOC_ACCESSIBLE,
					    NUMA_NO_NODE);
}

static inline void * __init memblock_virt_alloc_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_virt_alloc_try_nid_nopanic(size, align,
						    BOOTMEM_LOW_LIMIT,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    NUMA_NO_NODE);
}

#ifndef ARCH_LOW_ADDRESS_LIMIT
#define ARCH_LOW_ADDRESS_LIMIT  0xffffffffUL
#endif

static inline void * __init memblock_virt_alloc_low(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_virt_alloc_try_nid(size, align,
						   BOOTMEM_LOW_LIMIT,
						   ARCH_LOW_ADDRESS_LIMIT,
						   NUMA_NO_NODE);
}
static inline void * __init memblock_virt_alloc_low_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_virt_alloc_try_nid_nopanic(size, align,
						   BOOTMEM_LOW_LIMIT,
						   ARCH_LOW_ADDRESS_LIMIT,
						   NUMA_NO_NODE);
}

/* IAMROOT-12 fehead (2016-11-28):
 * --------------------------
 * pi2
 */
static inline void * __init memblock_virt_alloc_from_nopanic(
		phys_addr_t size, phys_addr_t align, phys_addr_t min_addr)
{
	return memblock_virt_alloc_try_nid_nopanic(size, align, min_addr,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    NUMA_NO_NODE);
}

static inline void * __init memblock_virt_alloc_node(
						phys_addr_t size, int nid)
{

/* IAMROOT-12AB:
 * -------------
 * BOOTMEM을 사용하지 않고 바로 memeblock을 사용하는 경우 BOOTMEM_LOW_LIMIT=0
 * BOOTMEM을 사용하는 경우 DMA 영역을 제외한 공간부터 할당 가능하다.
 */
	return memblock_virt_alloc_try_nid(size, 0, BOOTMEM_LOW_LIMIT,
					    BOOTMEM_ALLOC_ACCESSIBLE, nid);
}

/* IAMROOT-12 fehead (2017-01-02):
 * --------------------------
 * size = 0x3c000 * 0x24(36) = 0x870000(8847360, 8.44M), nid = 0
 */
static inline void * __init memblock_virt_alloc_node_nopanic(
						phys_addr_t size, int nid)
{
/* IAMROOT-12AB:
 * -------------
 * arm의 경우 bootmem을 사용하지 않으므로 BOOTMEM_LOW_LIMIT는 0이다.
 * (bootmem은 최근의 커널에서 대부분의 아키텍처에서 제거된 상태이다.)
 */
	return memblock_virt_alloc_try_nid_nopanic(size, 0, BOOTMEM_LOW_LIMIT,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    nid);
}

static inline void __init memblock_free_early(
					phys_addr_t base, phys_addr_t size)
{
	__memblock_free_early(base, size);
}

static inline void __init memblock_free_early_nid(
				phys_addr_t base, phys_addr_t size, int nid)
{
	__memblock_free_early(base, size);
}

static inline void __init memblock_free_late(
					phys_addr_t base, phys_addr_t size)
{
	__memblock_free_late(base, size);
}

#else

#define BOOTMEM_ALLOC_ACCESSIBLE	0


/* Fall back to all the existing bootmem APIs */
static inline void * __init memblock_virt_alloc(
					phys_addr_t size,  phys_addr_t align)
{
	if (!align)
		align = SMP_CACHE_BYTES;
	return __alloc_bootmem(size, align, BOOTMEM_LOW_LIMIT);
}

static inline void * __init memblock_virt_alloc_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	if (!align)
		align = SMP_CACHE_BYTES;
	return __alloc_bootmem_nopanic(size, align, BOOTMEM_LOW_LIMIT);
}

static inline void * __init memblock_virt_alloc_low(
					phys_addr_t size, phys_addr_t align)
{
	if (!align)
		align = SMP_CACHE_BYTES;
	return __alloc_bootmem_low(size, align, 0);
}

static inline void * __init memblock_virt_alloc_low_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	if (!align)
		align = SMP_CACHE_BYTES;
	return __alloc_bootmem_low_nopanic(size, align, 0);
}

static inline void * __init memblock_virt_alloc_from_nopanic(
		phys_addr_t size, phys_addr_t align, phys_addr_t min_addr)
{
	return __alloc_bootmem_nopanic(size, align, min_addr);
}

static inline void * __init memblock_virt_alloc_node(
						phys_addr_t size, int nid)
{
	return __alloc_bootmem_node(NODE_DATA(nid), size, SMP_CACHE_BYTES,
				     BOOTMEM_LOW_LIMIT);
}

static inline void * __init memblock_virt_alloc_node_nopanic(
						phys_addr_t size, int nid)
{
	return __alloc_bootmem_node_nopanic(NODE_DATA(nid), size,
					     SMP_CACHE_BYTES,
					     BOOTMEM_LOW_LIMIT);
}

static inline void * __init memblock_virt_alloc_try_nid(phys_addr_t size,
	phys_addr_t align, phys_addr_t min_addr, phys_addr_t max_addr, int nid)
{
	return __alloc_bootmem_node_high(NODE_DATA(nid), size, align,
					  min_addr);
}

static inline void * __init memblock_virt_alloc_try_nid_nopanic(
			phys_addr_t size, phys_addr_t align,
			phys_addr_t min_addr, phys_addr_t max_addr, int nid)
{
	return ___alloc_bootmem_node_nopanic(NODE_DATA(nid), size, align,
				min_addr, max_addr);
}

static inline void __init memblock_free_early(
					phys_addr_t base, phys_addr_t size)
{
	free_bootmem(base, size);
}

static inline void __init memblock_free_early_nid(
				phys_addr_t base, phys_addr_t size, int nid)
{
	free_bootmem_node(NODE_DATA(nid), base, size);
}

static inline void __init memblock_free_late(
					phys_addr_t base, phys_addr_t size)
{
	free_bootmem_late(base, size);
}
#endif /* defined(CONFIG_HAVE_MEMBLOCK) && defined(CONFIG_NO_BOOTMEM) */

#ifdef CONFIG_HAVE_ARCH_ALLOC_REMAP
extern void *alloc_remap(int nid, unsigned long size);
#else
/* IAMROOT-12 fehead (2017-01-02):
 * --------------------------
 * pi2
 */
static inline void *alloc_remap(int nid, unsigned long size)
{
	return NULL;
}
#endif /* CONFIG_HAVE_ARCH_ALLOC_REMAP */

extern void *alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries,
				     int scale,
				     int flags,
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long low_limit,
				     unsigned long high_limit);

#define HASH_EARLY	0x00000001	/* Allocating during early boot? */
#define HASH_SMALL	0x00000002	/* sub-page allocation allowed, min
					 * shift passed via *_hash_shift */

/* Only NUMA needs hash distribution. 64bit NUMA architectures have
 * sufficient vmalloc space.
 */
#if defined(CONFIG_NUMA) && defined(CONFIG_64BIT)
#define HASHDIST_DEFAULT 1
#else
#define HASHDIST_DEFAULT 0
#endif
extern int hashdist;		/* Distribute hashes across NUMA nodes? */


#endif /* _LINUX_BOOTMEM_H */
